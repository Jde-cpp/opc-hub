#include <jde/web/server/HttpRequest.h>
#include <absl/strings/match.h>
#include <jde/web/Gzip.h>
#include <jde/web/server/Server.h>
#include <jde/fwk/str.h>
#include <jde/fwk/chrono.h>

#define let const auto

namespace Jde::Web{
	α Server::AccessControlAllowOrigin()ι->str{
		static const string y = Settings::FindString( "/http/accessControl/allowOrigin" ).value_or( string{Server::SameHostOrigin} );
		return y;
	}

	string _plainVersion{ Ƒ("({})Jde.Web.Server - {}", Process::ProductVersion, BOOST_BEAST_VERSION) };
	string _sslVersion{ Ƒ("({})Jde.Web.Server SSL - {}", Process::ProductVersion, BOOST_BEAST_VERSION) };
	α Server::ServerVersion( bool isSsl )ι->str{ return isSsl ? _sslVersion : _plainVersion; }
}

namespace Jde::Web::Server{
	static atomic<uint32> _sequence = 0;

	HttpRequest::HttpRequest( TRequestType&& request, tcp::endpoint userEndpoint, bool isSsl, uint32 connectionId )ι:
		UserEndpoint{ move(userEndpoint) },
		_connectionId{ connectionId },
		_index{ ++_sequence },
		_isSsl{ isSsl },
		_request{ move(request) },
		_start{ steady_clock::now() }{
		ParseUri();
	}

	//host portion of an Origin ("https://h:1968") or a Host ("h:1968"): scheme and port stripped, ipv6 literal kept whole.
	Ω hostOf( sv value )ι->sv{ return Str::ParseUrl( value ).Host; }

	//Access-Control-Allow-Origin carries one value - "*", "null", or a single origin - so "same host, any port" cannot be written
	//as a header pattern; it has to be evaluated per request and the Origin reflected back.  that is the shape of this deployment:
	//AppServer, OpcGateway and the spa each on their own port of one host.
	//Comparing the Origin's host against this request's own Host holds up for the browser threat model: a page on evil.com reaching
	//us sends our host in Host and its own in Origin, so it fails.  Forging Host takes a non-browser client, which CORS never
	//constrained anyway.  Reflecting is only safe here because Access-Control-Allow-Credentials is never sent - auth rides an
	//explicit Authorization header - reflect-plus-credentials is the combination that hands an attacker the session.
	α HttpRequest::AllowOrigin()Ι->tuple<string,bool>{
		let& configured = Server::AccessControlAllowOrigin();
		if( configured!=Server::SameHostOrigin )
			return { configured, false };//literal "*" or a pinned origin: same answer for every caller, so nothing to vary on.
		let origin = Header( "origin" );
		let host = Header( "host" );
		let allowed = origin.size() && absl::EqualsIgnoreCase( hostOf(origin), hostOf(host) );
		if( origin.size() && !allowed )//the browser only shows an opaque 'Failed to fetch' - this side knows why the header was withheld.
			DBGT( ELogTags::Server | ELogTags::Http, "CORS: rejecting Origin '{}' for Host '{}' - allowOrigin='sameHost' requires matching hosts; pin /http/accessControl/allowOrigin or serve the page from '{}'.", origin, host, hostOf(host) );
		return { allowed ? origin : string{}, true };
	}

	α HttpRequest::operator[]( str x )Ι->const string&{
		auto p = _params.find( x );
		return p!=_params.end() ? p->second : Str::Empty();
	}

	α HttpRequest::Body()ε->jobject&{
		if( !_body ){
			if( auto& s = _request.body(); s.size() )
				_body = Json::Parse( move(s) );
			else
				_body.emplace();
		}
		return *_body;
	}
	//BodyLimit capped the compressed bytes the parser read; the inflated body gets the same cap, so a handler sees no more either way.
	α HttpRequest::DecodeBody()ε->void{
		string encoding;//repeated lines are one list (RFC 9110 §5.3) - gzip on two lines is a stacked coding, as "gzip, gzip" is.
		for( auto [p, end] = _request.equal_range(http::field::content_encoding); p!=end; ++p ){
			if( encoding.size() )
				encoding += ", ";
			encoding += p->value();
		}
		if( encoding.empty() )
			return;
		if( auto& body = _request.body(); body.size() && !absl::EqualsIgnoreCase(encoding, "identity") ){//an empty body has nothing to decode, whatever it claims.
			if( !IsGzip(encoding) ){
				ResponseHeaders.emplace( "Accept-Encoding", "gzip" );//RFC 9110 §12.5.3: a 415 for a content coding names the ones accepted.
				throw Exception{ SRCE_CUR, {EHttpStatus::UnsupportedMediaType}, "Content-Encoding '{}' is not supported.", encoding };
			}
			optional<string> inflated;
			try{
				inflated = Gunzip( body, BodyLimit() );
			}
			catch( Exception& e ){
				throw Exception{ SRCE_CUR, {EHttpStatus::BadRequest}, move(e), "Malformed gzip body." };
			}
			THROW_IFX( !inflated, Exception(SRCE_CUR, {EHttpStatus::PayloadTooLarge}, "The body inflates past the {} byte limit.", BodyLimit()) );
			body = move( *inflated );
			_request.content_length( body.size() );
		}
		_request.erase( http::field::content_encoding );//the handler gets an identity body either way, labelled as one.
	}
	α HttpRequest::GraphQLQuery()ε->std::pair<string,jobject>{
		string query; jobject vars;
		if( IsGet() ){
			query = (*this)["query"];
			if( let& variables = (*this)["variables"]; variables.size() )
				vars = Json::Parse( variables );
		}
		else{
			let& body = Body();
			if( auto jquery = body.if_contains("query"); jquery && jquery->is_string() )
				query = jquery->get_string();
			if( auto jvars = body.if_contains("variables"); jvars && jvars->is_object() )
				vars = jvars->get_object();
		}
		return { move(query), move(vars) };
	}
	α HttpRequest::ParseUri()->void{
		let uri = sv{ _request.target() };//split before decoding so encoded '&'/'=' in values don't corrupt parsing.
		let queryStart = uri.find( '?' );
		_target = Str::DecodeUri( uri.substr(0, queryStart) );
		if( queryStart!=sv::npos ){
			for( let& param : Str::Split(uri.substr(queryStart+1), '&') ){
				if( param.empty() )
					continue;
				let eq = param.find( '=' );
				_params[Str::DecodeUri(param.substr(0, eq))] = eq==sv::npos ? string{} : Str::DecodeUri( param.substr(eq+1) );
			}
		}
	}

	α HttpRequest::Response( jvalue j, SL sl )Ι->http::response<http::string_body>{
		auto y = Response<http::string_body>();
		y.body() = serialize( move(j) );
		y.prepare_payload();
		LOGSL( ELogLevel::Debug, sl, ELogTags::HttpServerWrite, "[{}.{}.{}]HttpResponse:  {}{} - {}", hex(SessionInfo ? SessionInfo->SessionId : 0), hex(_connectionId), hex(_index), Target(), y.body().substr(0, MaxLogLength()), Chrono::ToString<steady_clock::duration>(steady_clock::now()-_start) );
		return y;
	}

	//logged by content type and size where the json response logs its body - a file's first bytes say nothing.
	α HttpRequest::Response( string&& body, sv contentType, SL sl )Ι->http::response<http::string_body>{
		auto y = Response<http::string_body>();
		y.set( http::field::content_type, contentType );
		y.body() = move( body );
		y.prepare_payload();
		LOGSL( ELogLevel::Debug, sl, ELogTags::HttpServerWrite, "[{}.{}.{}]HttpResponse:  {} - {}, {} bytes - {}", hex(SessionInfo ? SessionInfo->SessionId : 0), hex(_connectionId), hex(_index), Target(), contentType, y.body().size(), Chrono::ToString<steady_clock::duration>(steady_clock::now()-_start) );
		return y;
	}

	α HttpRequest::SessionId()Ι->SessionPK{
		SessionPK sessionId{ SessionInfo ? SessionInfo->SessionId : 0 };
		if( auto authorization = sessionId ? string{} : Header("authorization"); authorization.size() )
			sessionId = Str::TryTo<SessionPK,16>( authorization ).value_or( 0 );

		return sessionId;
	}

	α HttpRequest::LogRead( str text, ELogLevel level, SL sl )Ι->void{
		LOGSL( level, sl, ELogTags::HttpServerRead, "[{:x}.{:x}.{:x}]HttpRequest:  {} - {} - {}", SessionId(), _connectionId, _index, Target(), text.substr(0, MaxLogLength()), Chrono::ToString<steady_clock::duration>(steady_clock::now()-_start) );
	}
}
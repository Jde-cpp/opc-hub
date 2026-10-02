#include <jde/web/server/HttpRequest.h>
#include <absl/strings/match.h>
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
	α Server::ServerVersion( bool isSsl )ι->string{ return isSsl ? _sslVersion : _plainVersion; }//TODO cache
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
	Ω hostOf( sv value )ι->sv{
		if( let scheme = value.find("://"); scheme!=sv::npos )
			value = value.substr( scheme+3 );
		if( value.starts_with('[') ){//ipv6 literal - the colons inside it are not a port separator.
			let close = value.find( ']' );
			return close==sv::npos ? value : value.substr( 0, close+1 );
		}
		let end = value.find_first_of( ":/" );
		return end==sv::npos ? value : value.substr( 0, end );
	}

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

	α HttpRequest::SessionId()Ι->SessionPK{
		SessionPK sessionId{ SessionInfo ? SessionInfo->SessionId : 0 };
		if( auto authorization = sessionId ? string{} : Header("authorization"); authorization.size() )
			sessionId = Str::TryTo<SessionPK>( authorization, 16 ).value_or( 0 );

		return sessionId;
	}

	α HttpRequest::LogRead( str text, ELogLevel level, SL sl )Ι->void{
		LOGSL( level, sl, ELogTags::HttpServerRead, "[{:x}.{:x}.{:x}]HttpRequest:  {} - {} - {}", SessionId(), _connectionId, _index, Target(), text.substr(0, MaxLogLength()), Chrono::ToString<steady_clock::duration>(steady_clock::now()-_start) );
	}
}
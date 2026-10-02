#include <execution>
#include <jde/web/client/client.h>//L5: Client::MaxLogLength
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include <jde/web/Jwt.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/fwk/str.h>
#include <jde/fwk/process/execution.h>
#include <jde/app/proto/app.FromServer.h>
#include <jde/app/proto/common.h>
#include <jde/access/AccessException.h>
#include "jde/fwk/exceptions/Exception.h"
#include "mocks/ServerMock.h"

#define let const auto

namespace Jde::Web{
	constexpr ELogTags _tags{ ELogTags::Test };
	using Mock::Host; using Mock::Port;

	struct WebTests : ::testing::Test{
	protected:
		WebTests():_requestHandler(ms<Mock::RequestHandler>(jobject{})) {}
		~WebTests() override{}

		Ω SetUpTestCase()->void;
		α SetUp()->void override{};
		α TearDown()->void override{}
		Ω TearDownTestCase()->void;

		sp<Server::IRequestHandler> _requestHandler;
	};
	constexpr sv ContentType{ "application/x-www-form-urlencoded" };
	static mutex _exceptionMutex;
	static up<Exception> _exception;
	Ω setException( Exception&& e )ι->void{
		lg _{ _exceptionMutex };
		if( !_exception )//keep the first: it carries the original context, and what follows is usually its fallout.
			_exception = e.Move();
	}
	Ω hasException()ι->bool{ lg _{ _exceptionMutex }; return _exception!=nullptr; }
	Ω takeException()ι->up<Exception>{ lg _{ _exceptionMutex }; return move(_exception); }

	α WebTests::SetUpTestCase()->void{
		Stopwatch _{ "WebTests::SetUpTestCase", _tags };
		Mock::Start( Settings::AsObject("/http") );
	}

	α WebTests::TearDownTestCase()->void{
		Stopwatch _{ "WebTests::TearDownTestCase", _tags };
		Mock::Stop();
	}
	using Web::Client::ClientHttpAwait;
	using Web::Client::ClientHttpRes;
	using Web::Client::ClientHttpResException;

	//access-review3 #29's mirror:  IRequestHandler::UserName has the same fallback, taken when no authorizer is registered.
	TEST_F( WebTests, UserNameFallbackPrintsThePk ){
		EXPECT_EQ( "4242", _requestHandler->UserName(UserPK{4242}) );
	}

	TEST_F( WebTests, IsSsl ){
		auto await = ClientHttpAwait{ Host, "/ping", Port, {.ContentType="text/ping", .Verb=http::verb::post} };
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		//Debug( _tags, "Headers.Size: {}", res.Headers().size() );
		ASSERT_TRUE( res[http::field::server].contains("SSL") );
	}

	TEST_F( WebTests, GoogleCerts ){
		auto await = ClientHttpAwait{ "www.googleapis.com", "/oauth2/v3/certs", 443, {.ContentType="", .Verb=http::verb::get} };
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		let certs = res.Json();
		ASSERT_TRUE( certs.contains("keys") );
		let keys = certs.at("keys").as_array();
		ASSERT_GT( keys.size(), 0 );
		ASSERT_TRUE( keys[0].is_object() );
	}
	TEST_F( WebTests, GZip ){
		auto await = ClientHttpAwait{ "en.wikipedia.org", string{"/wiki/Madden_NFL_26"}, 443, {.ContentType="", .Verb=http::verb::get} };
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		ASSERT_TRUE( res[http::field::content_encoding].contains("gzip") );
	}

	TEST_F( WebTests, IsPlain ){
		auto await = ClientHttpAwait{ Host, "/ping", Port, {.ContentType="text/ping", .Verb=http::verb::post, .IsSsl=false} };
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		ASSERT_FALSE( res[http::field::server].contains("SSL") );
	}

	TEST_F( WebTests, EchoAttack ){
		constexpr uint count=200; //windows seems to limit Socket.Listen(backlog) to 200.
		array<uint,count> indexes;
		for( uint i=0; i<count; ++i )
			indexes[i] = i;
		array<atomic<SessionPK>,count> sessionIds{};
		try{
			atomic<uint> connections = 0;
			std::for_each( indexes.begin(), indexes.end(), [&sessionIds,&connections]( uint index )mutable{
				[]( auto index, auto& sessionIds, auto& connections )->ClientHttpAwait::Task {
					if( hasException() )
						co_return;
					auto pSessionIds = &sessionIds;
					const uint idx = index;
					try{
						++connections;
						ClientHttpRes res = co_await ClientHttpAwait{ Host, Ƒ("/echo?{}", idx), Port };
						--connections;
						auto jsonResult = Json::Parse( res.Body() )["params"].at(0);
						let echoIndex = To<SessionPK>( Json::AsString(jsonResult) );
						if( echoIndex!=idx )
							THROW( "index={} echoIndex={}", idx, echoIndex );
						(*pSessionIds)[idx] = *Str::TryTo<SessionPK>( res[http::field::authorization], 16 );
					}
					catch( Exception& e ){
						DBG( "connections={}", connections.load() );
						setException( move(e) );
					}
				}( index, sessionIds, connections );
			});
			while( find_if(sessionIds, [](auto& s){return s.load()==0;})!=sessionIds.end() && !hasException() )
				std::this_thread::yield();
			if( auto e = takeException(); e )
				e->Throw();
			//std::for_each( std::execution::par_unseq, indexes.begin(), indexes.end(), [&sessionIds]( auto index )mutable{
			for_each( indexes, [&sessionIds]( auto index )mutable{
				[&sessionIds,index]()->ClientHttpAwait::Task{
					auto pSessionIds=&sessionIds;
					uint idx = index;
					let sessionId = (*pSessionIds)[idx].load();
					ClientHttpRes res = co_await ClientHttpAwait{ Host, "/Authorization", Port, {.Authorization=Ƒ("{:x}", sessionId)} };
					if( sessionId!=*Str::TryTo<uint>(res[http::field::authorization], 16) )
						THROW( "sessionId={} authorization={}", sessionId, res[http::field::authorization] );
					(*pSessionIds)[idx] = 0;
				}();
			});
		}
		catch( const Exception& e ){
			e.SetLevel( ELogLevel::Critical );
			e.Log();
			ASSERT_FALSE( true );
		}
		while( find_if(sessionIds, [](auto& s){return s.load()!=0;})!=sessionIds.end() )
			std::this_thread::yield();
	}
	TEST_F( WebTests, BadSessionId ){
		try{
			auto await = ClientHttpAwait{ Host, "/echo?InvalidSessionId", Port, {.Authorization="xxxxxx"} };
			let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
			ASSERT_FALSE( true );
		}
		catch( ClientHttpResException& e){
			ASSERT_EQ( http::status::unauthorized, e.Status() );
		}
		catch( runtime_error& e){
			ASSERT_FALSE( true );
		}
	}
	TEST_F( WebTests, CloseMidRequest ){
		namespace beast = boost::beast;
		net::any_io_executor strand = net::make_strand( *Executor() );
		tcp::resolver resolver{ strand };
    auto stream = mu<beast::tcp_stream>( strand );
		let results = resolver.resolve( Host, std::to_string(Port) );
    stream->connect( results );
		uint delay = 2;
		http::request<http::empty_body> req{ http::verb::get, Ƒ("/delay?seconds={}", delay), 11 };
		req.set( http::field::content_type, ContentType );
		absl::Notification readDone;
		auto onWrite = []( beast::error_code ec, uint /*bytes_transferred*/ )ι{
			ASSERT( !ec );
			DBG( "onWrite" );
		};
		net::post( strand, [&]{
    	http::async_write( *stream, req, onWrite );
		});
		auto onRead = [&]( beast::error_code ec, uint /*bytes_transferred*/ )ε{
			CodeException{ ec, _tags }; //expected.
			readDone.Notify();
		};
		beast::flat_buffer buffer;
		http::request_parser<http::string_body> parser;
		http::async_read( *stream, buffer, parser, onRead );
		//std::this_thread::sleep_for( std::chrono::seconds{1} );
		net::post( strand, [&]{
			beast::error_code ec;
			stream->socket().shutdown( tcp::socket::shutdown_both, ec );//TODO use cancellation token.
			ASSERT( !ec );
			stream->socket().close( ec );
			ASSERT( !ec );
			stream = nullptr;
			DBG( "client stream shutdown" );
		});
		readDone.WaitForNotification();//a Notification, not a predicate-less cv.wait:  a read that completes first is not lost.
		std::this_thread::sleep_for( std::chrono::seconds{delay}+500ms );
		//TODO rest stream write succeeds even though stream is shutdown.
	}
	TEST_F( WebTests, BadTarget ){
		try{
			auto await = ClientHttpAwait{ Host, "/BadTarget", Port };
			let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		}
		catch( const ClientHttpResException& e ){
			ASSERT_EQ( http::status::not_found, e.Status() );
		}
	}
	TEST_F( WebTests, BadAwaitable ){
		try{
			auto await = ClientHttpAwait{ Host, "/BadAwaitable", Port };
			let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
			ASSERT_FALSE( true );
		}
		catch( const ClientHttpResException& e ){
			ASSERT_EQ( http::status::internal_server_error, e.Status() );
		}
	}
	//#3: the error funnel has to build its response from the await's request - the local was moved into HandleRequest, and a
	//moved-from one carries no SessionInfo, so an initial request would fail without ever being told its session id.
	TEST_F( WebTests, ErrorResponseKeepsSession ){
		try{
			let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/NoResult", Port} );
			ASSERT_FALSE( true ) << "expected internal_server_error, got " << (uint32)res.Status();
		}
		catch( const ClientHttpResException& e ){
			ASSERT_EQ( http::status::internal_server_error, e.Status() );
			let authorization = e.Res()[http::field::authorization];
			let sessionId = Str::TryTo<SessionPK>( authorization, 16 );
			ASSERT_TRUE( sessionId && *sessionId ) << Ƒ( "authorization='{}'", authorization );
		}
	}
	TEST_F( WebTests, TestTimeout ){
		let testStartTime = Chrono::ToClock<Clock,steady_clock>( steady_clock::now() );
		let timeoutString = Settings::FindSV("/http/timeout").value_or( "PT30S" );
		let timeout = Chrono::ToDuration( timeoutString );
		ASSERT( timeout<=30s );//too long to wait.

		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/timeout", Port} );//fetch timeout
		let currentTimeoutString = Json::AsString( res.Json(), "value" );//
		let currentTimeout = Chrono::ToTimePoint( currentTimeoutString );
		DBG( "Expected: ({}+{}) '{}'  Actual:  '{}'", ToIsoString(testStartTime), timeoutString, ToIsoString(testStartTime+timeout), ToIsoString(currentTimeout) );
		ASSERT_LE( testStartTime+timeout-1s, currentTimeout );
		let authorization = res[http::field::authorization];

		auto await2 = ClientHttpAwait{ Host, "/timeout", Port, {.Authorization=authorization} };
		let res2 = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await2) );
		let nextSystemEndTime = Chrono::ToTimePoint( Json::AsString(Json::Parse(res2.Body()), "value") );
		ASSERT_GT( nextSystemEndTime, testStartTime );
		DBG( "newTimeout:  '{}'", ToIsoString(nextSystemEndTime) );

		std::this_thread::sleep_for( timeout+1s );
		DBG( "TestTimeout:  '{}'", ToIsoString(Clock::now()) );
		try{
			let res3 = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/timeout", Port, {.Authorization=authorization}} );
			ASSERT_FALSE( true );
		}
		catch( const ClientHttpResException& e ){
			ASSERT_EQ( http::status::unauthorized, e.Status() );
		}
	}
	//#7: LogRead & UserPK() ran straight through a null SessionInfo.  every call site happens to resolve the session first,
	//but nothing enforces it - UpsertAwait itself returns a null sp when constructed with throw_=false.
	TEST( HttpRequestTests, NoSessionInfo ){
		Server::HttpRequest req{ Server::TRequestType{}, tcp::endpoint{}, false, 0 };
		ASSERT_EQ( nullptr, req.SessionInfo );
		EXPECT_FALSE( req.UserPK() ); //anonymous, not a deref.
		EXPECT_EQ( 0u, req.SessionId() );
		req.LogRead( "no session" );//used to segfault.
	}

	//with no session resolved the log line falls back to what the client asked for.
	TEST( HttpRequestTests, SessionIdFromHeader ){
		Server::TRequestType raw;
		raw.set( http::field::authorization, "1a2b" );
		Server::HttpRequest req{ move(raw), tcp::endpoint{}, false, 0 };
		EXPECT_EQ( 0x1a2bu, req.SessionId() );
		req.LogRead( "unresolved session" );
	}

	//web-review3 L5: the lookup read "http/maxLogLength" with no leading slash, so Settings never matched it and the 255 default
	//always won - the setting was inert on the client side while the server read the same key correctly.
	TEST( ClientSettingsTests, MaxLogLengthReadsTheSetting ){
		let configured = Settings::FindNumber<uint16>( "/http/maxLogLength" );
		ASSERT_TRUE( configured ) << "Web.Tests.jsonnet sets /http/maxLogLength; without it this proves nothing";
		EXPECT_EQ( *configured, Client::MaxLogLength() ) << "the client must read the same key the server does";
	}

	//web-review3 L2: ParseUri split each param on *every* '=' and only took the value when it got exactly two pieces, so any value
	//containing one - base64 padding, a nested query, a jwt - was silently replaced by the empty string.  RFC 3986 gives '=' no
	//meaning inside a value: the first one separates, the rest are data.
	TEST( HttpRequestTests, ParamValueMayContainEquals ){
		Server::TRequestType raw;
		//Literal '=' in the values, not %3D: ParseUri splits before decoding, so a percent-encoded one never reached the bug.
		//Unencoded is legal (RFC 3986 sub-delims) and is how base64 padding usually arrives.
		raw.target( "/graphql?a=1&b64=YWJjZA==&nested=x=y&enc=p%3Dq&empty=&bare&last=end" );
		Server::HttpRequest req{ move(raw), tcp::endpoint{}, false, 0 };
		let& params = req.Params();
		EXPECT_EQ( "1", params.at("a") );
		EXPECT_EQ( "YWJjZA==", params.at("b64") ) << "base64 padding is the common case that vanished";
		EXPECT_EQ( "x=y", params.at("nested") ) << "only the first '=' separates; the rest are data";
		EXPECT_EQ( "p=q", params.at("enc") ) << "and a percent-encoded one still decodes";
		EXPECT_EQ( "", params.at("empty") ) << "an explicit empty value stays empty";
		EXPECT_EQ( "", params.at("bare") ) << "a valueless param is still the empty string";
		EXPECT_EQ( "end", params.at("last") ) << "parsing continues past the awkward ones";
	}

	//C1: the client verifies peers now.  The mock's self-signed cert is a trust anchor (Web::Server::Start self-registers it) and names
	//DNS:localhost,IP:127.0.0.1 - trusting it must not make it acceptable for any other host we happen to dial.
	TEST_F( WebTests, TlsVerifiesHostName ){
		auto ping = []( string host ){ return ClientHttpAwait{ move(host), "/ping", Port, {.ContentType="text/ping", .Verb=http::verb::post} }; };
		//in the SAN, same server, same anchor - the control that says the rejection below is about the name and nothing else.
		EXPECT_NO_THROW( (BlockAwait<ClientHttpAwait,ClientHttpRes>( ping("127.0.0.1") )) );
		//another loopback address the certificate does not name.  before this the handshake accepted whatever was offered.
		EXPECT_ANY_THROW( (BlockAwait<ClientHttpAwait,ClientHttpRes>( ping("127.0.0.2") )) );
	}

	//C4: redirects were followed recursively with no hop limit, and the full args - Authorization included - were re-sent to
	//whatever host the Location header named.
	TEST_F( WebTests, RedirectLimit ){
		//the mock answers /redirectLoop with a 302 back to itself; without a budget this never returns.
		EXPECT_ANY_THROW( (BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/redirectLoop", Port} )) );

		//AllowRedirects=false hands the 3xx back instead of following it - there used to be no way to ask for that.
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/redirectLoop", Port, {.AllowRedirects=false}} );
		EXPECT_EQ( http::status::found, res.Status() );
	}

	//web-review3 L3: OnRead is a coroutine whose Task nobody holds, so anything that escapes it lands in unhandled_exception on a
	//discarded task and the caller is never resumed - the request hangs instead of failing.  Two ways in: the ClientHttpRes ctor
	//inflates a gzip body outside the try, and RedirectVariables' std::stoul throws std::out_of_range - a logic_error, which the
	//catch( runtime_error& ) missed (the same gap #3 closed on the server side).
	TEST_F( WebTests, MalformedRedirectFailsRatherThanHangs ){
		EXPECT_ANY_THROW( (BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/redirectBadPort", Port} )) );
	}

	//web-review3 L4: IsSsl was fixed when the session was constructed and a redirect copied the original args, so an http->https
	//Location (or the reverse) was followed on the original transport - sessions are keyed on (host, port, IsSsl), so the hop
	//reached the right host and port over the wrong scheme.  The mock answers both schemes on one port, so the Location here
	//changes nothing but the scheme.  The Server response header is what names the transport that actually served it.
	TEST_F( WebTests, RedirectFollowsLocationScheme ){
		let direct = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/authHeader", Port} );
		ASSERT_TRUE( direct[http::field::server].contains("SSL") ) << "the control: reached directly, this request is served over SSL";

		let redirected = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/redirectPlain", Port} );
		EXPECT_FALSE( redirected[http::field::server].contains("SSL") ) << "an http:// Location must be followed over http, not the original scheme";
	}

	TEST_F( WebTests, RedirectDropsAuthorizationCrossHost ){
		//a real session id, not a made-up string: an unknown one is rejected as 401 before the handler runs, and the session id is
		//precisely the credential at stake here.
		let seed = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/echo?seed", Port} );
		let authorization = seed[http::field::authorization];
		ASSERT_FALSE( authorization.empty() );

		//same server, reached by a name its certificate also covers, so only the host string differs - which is exactly the
		//condition under which a credential must not travel.
		let crossHost = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/redirectHost", Port, {.Authorization=authorization}} );
		EXPECT_EQ( "", Json::AsString(crossHost.Json(), "authorization") ) << "Authorization followed a redirect to another host";

		//control: reaching the same target directly still carries it, so the assertion above is about the redirect, not the echo.
		let direct = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{"127.0.0.1", "/authHeader", Port, {.Authorization=authorization}} );
		EXPECT_EQ( authorization, Json::AsString(direct.Json(), "authorization") );
	}

	TEST_F( WebTests, BodyLimit ){
		let limit = Settings::FindNumber<uint>( "/http/bodyLimit" ).value_or( 0 );
		ASSERT_TRUE( limit && limit<10000 ) << "the configured limit has to sit below the old hard-coded 10000 or the over-limit case proves nothing";
		//extra parens: the comma in BlockAwait<A,B> would otherwise split the macro's argument list.
		let post = []( uint size ){ return ClientHttpAwait{ Host, "/ping", string(size, 'x'), Port, {.ContentType="text/plain", .Verb=http::verb::post} }; };

		EXPECT_NO_THROW( (BlockAwait<ClientHttpAwait,ClientHttpRes>( post(limit/2) )) );
		//past the cap beast fails the read and drops the connection instead of answering, so the client sees a throw, not a 413.
		EXPECT_ANY_THROW( (BlockAwait<ClientHttpAwait,ClientHttpRes>( post(limit+1024) )) );
	}

	//web-review3 O3: #14's remnant.  SendOptions answered a hardcoded `Access-Control-Allow-Headers: *` while every config in the
	//tree declared /http/accessControl/allowHeaders and allowMethods - written, and read by nobody.  `*` is not honoured as a
	//wildcard by browsers once a request carries credentials, which an authorized request here does, so it was both lax and
	//ineffective.
	TEST_F( WebTests, CorsPreflightUsesConfiguredHeaders ){
		let headers = Settings::FindString( "/http/accessControl/allowHeaders" );
		let methods = Settings::FindString( "/http/accessControl/allowMethods" );
		ASSERT_TRUE( headers && methods ) << "Web.Tests.jsonnet declares both; without them this proves nothing";
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/echo?cors", Port, {.Verb=http::verb::options}} );
		EXPECT_EQ( *headers, res[http::field::access_control_allow_headers] ) << "the preflight must answer what the deployment configured";
		EXPECT_NE( "*", res[http::field::access_control_allow_headers] ) << "and never the wide-open value #14 set out to close";
		EXPECT_EQ( *methods, res[http::field::access_control_allow_methods] );
	}

	//#14: Access-Control-Allow-Origin was a flat "*".  it carries one value, so "same host, any port" can only be done by
	//reflecting the request's Origin - the deployment is AppServer 1967, OpcGateway 1968 and the spa each on their own port.
	TEST_F( WebTests, CorsSameHostAnyPort ){
		let sameHost = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/echo?cors", Port, {.Origin=Ƒ("https://{}:9999", Host)}} );
		EXPECT_EQ( Ƒ("https://{}:9999", Host), sameHost[http::field::access_control_allow_origin] );
		EXPECT_EQ( "Origin", sameHost[http::field::vary] ) << "reflected without Vary, so a cache can hand this to another origin";

		//a different host must get no header at all - the browser blocks on its absence.
		let foreign = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/echo?cors", Port, {.Origin="https://evil.example.com"}} );
		EXPECT_EQ( "", foreign[http::field::access_control_allow_origin] );
		EXPECT_EQ( "Origin", foreign[http::field::vary] ) << "the refusal is origin-specific too, so it still varies";

		//no Origin at all is not a cross-origin request; nothing to reflect, and it must not fall back to "*".
		let none = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/echo?cors", Port} );
		EXPECT_EQ( "", none[http::field::access_control_allow_origin] );
	}

	//#11: a jwt with no `exp` was accepted forever - App::Client::getJwt mints exactly that for certificate login and posts it as a
	//bearer credential.  it is now bounded by `iat` instead, but only when there is no exp: a google id token, or the jwt
	//ServerSocketSession::Login replays, carries an hours-old iat next to a still-valid exp, so the window must not reach those.
	//the parser does not verify the signature, so these hand-built tokens need no key.
	Ω encodeJwt( jobject body )ι->string{
		let head = jobject{ {"alg","RS256"}, {"typ","JWT"} };
		return Str::Encode64( serialize(head), true )+"."+Str::Encode64( serialize(body), true )+"."+Str::Encode64( "notVerifiedHere"s, true );
	}
	Ω parseJwt( jobject body )ε->Jwt{ return Jwt{ encodeJwt(move(body)) }; }

	TEST( JwtExpirationTests, UnboundedTokenRejected ){
		let now = time( nullptr );
		constexpr time_t stale{ Jwt::MaxAgeWithoutExpiration+60 };

		EXPECT_NO_THROW( parseJwt({{"iat", now}}) );              //fresh & exp-less: certificate login, still works.
		EXPECT_THROW( parseJwt({{"iat", now-stale}}), Exception );//the hole - this was replayable forever.
		EXPECT_THROW( parseJwt({{"iat", now+stale}}), Exception );//future-dated iat would otherwise age into validity.

		//an exp is authoritative on its own; the iat window must not touch these or google login and socket re-auth break.
		EXPECT_NO_THROW( parseJwt({{"iat", now-stale}, {"exp", now+3600}}) );
		EXPECT_THROW( parseJwt({{"iat", now}, {"exp", now-1}}), Exception );
	}

	//A token's description is the enrolling client's own account of itself, written to users.description as-is.  It used to fall
	//back to the key's md5 fingerprint, restating the modulus/exponent stored on the same row; a token without one is empty now.
	TEST( JwtDescriptionTests, TakenVerbatimOrEmpty ){
		let now = time( nullptr );
		EXPECT_EQ( "Jde.OpcGateway 'Debug' on host", parseJwt({{"iat", now}, {"description", "Jde.OpcGateway 'Debug' on host"}}).Description );
		EXPECT_EQ( "", parseJwt({{"iat", now}}).Description ) << "nothing claimed - nothing invented";
		let keyed = parseJwt( {{"iat", now}, {"n", Str::Encode64("modulus"s, true)}, {"e", Str::Encode64("\x01\x00\x01"s, true)}} );//a key without a description used to get the fingerprint
		EXPECT_EQ( "", keyed.Description );
		EXPECT_FALSE( keyed.UserName.empty() ) << "the fingerprint still names an anonymous key";
	}

	//web-review3 O4: the parse gate above bounds when a token may be *presented*; Expires() is what bounds the session a consumer
	//mints from it, and for an exp-less token it answered TimePoint::max().  UAAccess::ActivateSession feeds it straight into
	//SessionContext::Expiration, so a certificate-login token good for ten minutes bought an OPC session good forever.
	TEST( JwtExpirationTests, ExpLessTokenReportsTheIatBound ){
		let now = time( nullptr );
		let expLess = parseJwt( {{"iat", now}} );
		EXPECT_NE( TimePoint::max(), expLess.Expires() ) << "an exp-less token must not report an unbounded lifetime";
		EXPECT_EQ( Clock::from_time_t(now+Jwt::MaxAgeWithoutExpiration), expLess.Expires() ) << "it is bounded by the same iat window Jwt::Jwt enforces";

		let expiring = parseJwt( {{"iat", now}, {"exp", now+3600}} );
		EXPECT_EQ( Clock::from_time_t(now+3600), expiring.Expires() ) << "an explicit exp is still authoritative";
	}

	//#15: the response body picks up the wrapped exception's ClientDetail, so every funnel surfaces a proc-raised
	//message without repeating the policy - and #5 stays honoured: the statement is not part of that detail.
	TEST( RestExceptionTests, AppDetailReachesBody ){
		DB::Sql sql; sql.Text = "exec access_user_insert_key ?,?";
		DB::DBException inner{ DB::EDbError::App, move(sql), "Slug 'x' already exists.", {0}, SRCE_CUR };
		Server::RestException e{ EHttpStatus::Unauthorized, move(inner), Server::HttpRequest{Server::TRequestType{}, tcp::endpoint{}, false, 0}, "Could not get sessionInfo." };

		let body = e.Response().body();
		EXPECT_TRUE( body.contains("Could not get sessionInfo.") ) << body;
		EXPECT_TRUE( body.contains("Slug 'x' already exists.") ) << body;
		EXPECT_FALSE( body.contains("access_user_insert_key") ) << body; //#5: the statement is not client text.
	}

	//engine errors name our schema, so only the proc-raised class is surfaced.
	TEST( RestExceptionTests, NonAppDetailWithheld ){
		DB::DBException inner{ DB::EDbError::Duplicate, DB::Sql{}, "UNIQUE constraint failed: access_identities.slug", {ELogLevel::NoLog, {}, 2067}, SRCE_CUR };
		Server::RestException e{ EHttpStatus::Unauthorized, move(inner), Server::HttpRequest{Server::TRequestType{}, tcp::endpoint{}, false, 0}, "Could not get sessionInfo." };

		let body = e.Response().body();
		EXPECT_EQ( body, "Could not get sessionInfo." ) << body;
	}

	//the DB classification and http status have to survive the AppServer round trip - the type itself cannot; ServerImpl answers with the reconstructed exception's EHttpStatus().
	TEST( AppExceptionProtoTests, DbErrorSurvivesRoundTrip ){
		DB::DBException source{ DB::EDbError::App, DB::Sql{}, "Slug 'x' already exists.", {ELogLevel::NoLog, {}, 0}, SRCE_CUR };
		let t = App::FromServer::Exception( source, RequestId{1} );
		auto e = App::ProtoUtils::ToException( Jde::Proto::Exception{t.messages(0).exception()} );

		let p = dynamic_cast<DB::DBException*>( e.get() );
		ASSERT_NE( nullptr, p ) << "flattened to a plain Exception - the ClientDetail policy would miss it";
		EXPECT_EQ( DB::EDbError::App, p->Error );
		EXPECT_EQ( 400u, p->HttpStatus() ); //App: a proc rejected the request - the client's fault.
		EXPECT_TRUE( string{p->what()}.contains("already exists") ) << p->what();
	}

	TEST( AppExceptionProtoTests, NonDbStaysPlain ){
		Exception source{ "not a db error", {ELogLevel::NoLog} };
		let t = App::FromServer::Exception( source, RequestId{1} );
		auto e = App::ProtoUtils::ToException( Jde::Proto::Exception{t.messages(0).exception()} );
		EXPECT_EQ( nullptr, dynamic_cast<DB::DBException*>(e.get()) );
		EXPECT_EQ( 500u, e->HttpStatus() );
	}

	//the concrete type (AccessException) has no counterpart in the reconstruction - the stored status is all that keeps a 403 a 403.
	//access-review3 #17:  a denial is Forbidden, not Unauthorized - the client's 401 policy is "the credential is stale" and logged
	//a merely-unprivileged user out.  Only the unknown-executer case still says 401, and says so explicitly.
	TEST( AppExceptionProtoTests, StatusSurvivesRoundTrip ){
		Access::AccessException source{ SRCE_CUR, UserPK{1}, "denied" };
		source.SetLevel( ELogLevel::NoLog );
		EXPECT_EQ( EHttpStatus::Forbidden, source.HttpStatus() );
		let t = App::FromServer::Exception( source, RequestId{1} );
		EXPECT_EQ( 403u, t.messages(0).exception().status_code() );

		auto e = App::ProtoUtils::ToException( Jde::Proto::Exception{t.messages(0).exception()} );
		EXPECT_EQ( nullptr, dynamic_cast<Access::AccessException*>(e.get()) );
		EXPECT_EQ( 403u, e->HttpStatus() );

		Access::AccessException unknown{ SRCE_CUR, UserPK{0}, EHttpStatus::Unauthorized, "User not found." };
		unknown.SetLevel( ELogLevel::NoLog );
		EXPECT_EQ( 401u, App::FromServer::Exception( unknown, RequestId{2} ).messages(0).exception().status_code() );
	}

	//status decided at run time, e.g. off the wire - RestException<TStatus> bakes it into the type.
	TEST( RestExceptionTests, RuntimeStatus ){
		Exception inner{ "boom", {ELogLevel::NoLog} };
		inner.SetHttpStatus( EHttpStatus::Conflict );
		Server::RestException e{ inner.HttpStatus(), move(inner), Server::HttpRequest{Server::TRequestType{}, tcp::endpoint{}, false, 0}, "Query failed." };
		EXPECT_EQ( EHttpStatus::Conflict, e.HttpStatus() );
		EXPECT_EQ( EHttpStatus::Conflict, static_cast<Exception&>(e).HttpStatus() ); //the catch(Exception&) funnel reads the same status.
		EXPECT_EQ( EHttpStatus::Conflict, (EHttpStatus)e.Response().result() );
	}
//TODO! gzip
//TODO Test redirect.
//TODO keep alives
//AppServer
		//TODO test logout.
}
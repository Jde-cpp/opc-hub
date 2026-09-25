//#include <boost/beast/ssl.hpp>
#include <jde/web/client/ClientSsl.h>
#include "jde/fwk/co/Await.h"
#include "jde/fwk/usings.h"
#include "mocks/ServerMock.h"
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/Jwt.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/utils/mathUtils.h>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/process/execution.h>
#include "mocks/ClientSocketSession.h"
#include <jde/web/server/IHttpRequestAwait.h>
#include <jde/web/server/Sessions.h>
#include <jde/web/client/socket/ClientSocketAwait.h>

#define let const auto
namespace Jde::Web{
	constexpr ELogTags _tags{ ELogTags::Test };
	using Client::ClientSocketAwait;
	using Client::ClientHttpAwait;
	using Client::ClientHttpRes;

	using Mock::Host; using Mock::Port;

	struct SocketTests : ::testing::Test{
	protected:
		SocketTests():_pRequestHandler(ms<Mock::RequestHandler>(jobject{})) {}
		~SocketTests() override{}

		Ω SetUpTestCase()->void;
		α SetUp()->void override;
		α TearDown()->void override;
		Ω TearDownTestCase()->void;

		sp<Server::IRequestHandler> _pRequestHandler;
	};

	sp<Mock::ClientSocketSession> _clientSession{};
	SessionPK _sessionId;
	α SocketTests::SetUpTestCase()->void{
		Stopwatch _{ "SocketTests::SetUpTestCase", ELogTags::Test };
		Mock::Start( Settings::AsObject("/http") );
	}
	α SocketTests::TearDownTestCase()->void{
		Stopwatch _{ "SocketTests::TearDownTestCase", ELogTags::Test };
		Mock::Stop();
	}

	α SocketTests::SetUp()->void{
		Logging::ClearMemory();
	}
	//_notified + a predicate, not a bare notify_one: a completion that lands before Wait() blocks is otherwise lost and the test
	//hangs instead of the code.  That happens whenever a close or a request finishes synchronously - which C3 and C6 both made
	//routine.
	#define NOTIFY sl l{ _mutex }; _notified = true; cv.notify_one()
	std::shared_mutex _mutex;
	std::condition_variable_any cv;
	bool _notified{};
	α Notify()ι{
		sl l{ _mutex };
		_notified = true;
		cv.notify_one();
	}
	α Close()ι->VoidTask{
		co_await _clientSession->Close( true, SRCE_CUR );
		Notify();
	}

	α Wait()ι{
		sl l{ _mutex };
		cv.wait( l, []{ return _notified; } );
		_notified = false;
	}

	α SocketTests::TearDown()->void{
		if( _clientSession ){
			Close();
			Wait();
			TRACE( "_clientSession.use_count()={}", _clientSession.use_count() );
			//ASSERT( _clientSession.use_count()==1 );
			_clientSession = nullptr;
			TRACE( "_clientSession.use_count()={}", _clientSession.use_count() );
		}
		_sessionId = 0;
	}

	static up<Exception> _exception;
	Ω connect()->SessionPK{
		return BlockAwait<ClientSocketAwait<SessionPK>,SessionPK>( _clientSession->Connect( _sessionId ) );
	}
	Ω login()->void{
		if( _sessionId )
			return;
		Crypto::CryptoSettings settings{ "http/ssl" };//the client signs the jwt with its own key pair - the server's 'web.tests' identity is not the caller's.
		if( !fs::exists(settings.PrivateKey.Path) || !fs::exists(settings.PublicKey.Path) ){//first run on a machine; no certificate needed, the mock '/login' verifies the signature against the jwt's key.
			settings.CreateDirectories();
			Crypto::CreateKey( settings, SRCE_CUR );
		}
		auto publicKey = Crypto::ReadPublicKey( settings.PublicKey.Path );
		Web::Jwt jwt{ move(publicKey), {0}, "testUser", "testUserCallSign", 0, "127.0.0.1", Clock::now()+1h, {}/*description*/, settings.PrivateKey };
		auto await = ClientHttpAwait{ Host, "/login", serialize(jobject{{"jwt", jwt.Payload()}}), Port };
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		_sessionId = *Str::TryTo<SessionPK>( res[http::field::authorization], nullptr, 16 );
		INFO( "({:x})Loggin Complete.", _sessionId );
	}
	Ω connectSocket( optional<ssl::context> ctx=nullopt )->void{
		_clientSession = ms<Mock::ClientSocketSession>( Executor(), ctx );
		BlockVoidAwait( _clientSession->RunSession( Host, Port ) );
		connect();
	}
	Ω createSession( optional<ssl::context> ctx=nullopt )->void{
		login();
		connectSocket( move(ctx) );
	}
	TEST_F( SocketTests, CreatePlain ){
		Stopwatch sw{ "WebTests::CreatePlain", ELogTags::Test };
		createSession();
		ASSERT_EQ( _sessionId, _clientSession->SessionId() );
	}

	TEST_F( SocketTests, CreateSsl ){
		std::this_thread::sleep_for( 1s );
		TRACET( ELogTags::Test, "WebTests::CreateSsl" );
		Stopwatch sw{ "WebTests::CreateSsl", ELogTags::Test };
		createSession( Client::Ssl::MakeContext() );
		ASSERT_EQ( _sessionId, _clientSession->SessionId() );
	}

	//A raw session id is only a credential from the endpoint it was minted for.  Resuming it from a different address must be
	//denied - otherwise a guessed or leaked id is a full account takeover (appserver-review2 #3, Sessions.cpp UpdateExpiration).
	//Driven through the Sessions API rather than the socket because both ends of a localhost socket share 127.0.0.1.
	TEST_F( SocketTests, SessionEndpointBinding ){
		using Server::SessionInfo; using Server::Sessions::UpsertAwait;
		let info = Server::Sessions::Add( Jde::UserPK{7}, "10.9.9.1", true );//session bound to 10.9.9.1.
		let id = Ƒ("{:x}", info->SessionId);
		let resume = []( str authorization, str endpoint )->sp<SessionInfo> {
			return BlockAwait<UpsertAwait,sp<SessionInfo>>( UpsertAwait{ authorization, endpoint, true, nullptr } );
		};
		EXPECT_THROW( resume(id, "10.9.9.2"), Exception ) << "a session id must not resume from a different endpoint";
		let same = resume( id, "10.9.9.1" );
		ASSERT_TRUE( same ) << "the minting endpoint must still resume the session";
		EXPECT_EQ( info->SessionId, same->SessionId );
		Server::Sessions::Remove( info->SessionId );
	}

	//web-review3 #10: FromSessionId's catch treated every SessionInfoAwait failure as "anonymous user" and cached it under the
	//caller's real session id, so an AppServer restart or its 60s request-deadline close - hit while a logged-in user was on the
	//gateway - turned that user anonymous for RestSessionTimeout, with UpdateExpiration refreshing the entry instead of ever
	//asking again.  Only a negative answer may be cached.
	TEST_F( SocketTests, TransportFailureNotCachedAsAnonymous ){
		using Server::SessionInfo; using Server::Sessions::UpsertAwait;
		let resume = []( SessionPK id, EHttpStatus status )->sp<SessionInfo> {
			return BlockAwait<UpsertAwait,sp<SessionInfo>>( UpsertAwait{ Ƒ("{:x}", id), "10.9.9.3", true, Mock::FailingAppClient(status) } );
		};
		constexpr SessionPK transportId{ 0x7A11 };
		EXPECT_THROW( resume(transportId, EHttpStatus::InternalServerError), Exception ) << "'could not ask' must surface, not answer anonymous";
		EXPECT_FALSE( Server::Sessions::Find(transportId) ) << "a transport failure must not seed _sessions - the entry would then answer from cache without ever asking again";

		constexpr SessionPK missingId{ 0x7A12 };
		let anon = resume( missingId, EHttpStatus::NotFound );//'no such session' - the case the anonymous cache is actually for.
		ASSERT_TRUE( anon ) << "a negative answer still resolves to an anonymous session";
		EXPECT_EQ( Jde::UserPK{0}, anon->UserPK );
		EXPECT_TRUE( Server::Sessions::Find(missingId) ) << "and it is cached, so a repeated dead id does not re-ask every request";
		Server::Sessions::Remove( missingId );
	}

	//The other half of the guard above (web-review3 #4).  UpdateExpiration only covers what is already in _sessions; when it denies
	//or misses, FromSessionId falls back to the 3rd party, which answers a bare Find with no endpoint check of its own.  Taking that
	//answer and rebinding UserEndpoint to the requester turned a sniffed id into a takeover, and cached it locally so every later
	//request hit the rebound entry.  Uses a non-local IApp - Mock::AppClient()'s IsLocal() short-circuits the fallback entirely.
	TEST_F( SocketTests, SessionEndpointBindingViaAppServer ){
		using Server::SessionInfo; using Server::Sessions::UpsertAwait;
		constexpr SessionPK sessionId{ 0x519E };//not in _sessions - the attacker's id is one the gateway has never seen.
		let id = Ƒ( "{:x}", sessionId );
		let resume = []( str authorization, str endpoint, str mintedAt )->sp<SessionInfo> {
			return BlockAwait<UpsertAwait,sp<SessionInfo>>( UpsertAwait{ authorization, endpoint, true, Mock::ForeignAppClient(string{mintedAt})} );
		};
		EXPECT_THROW( resume(id, "10.9.9.2", "10.9.9.1"), Exception ) << "a 3rd party answer for another address must not be accepted";
		EXPECT_FALSE( Server::Sessions::Find(sessionId) ) << "a denied id must not be cached locally";

		let same = resume( id, "10.9.9.1", "10.9.9.1" );
		ASSERT_TRUE( same ) << "the minting endpoint must still resume through the fallback";
		EXPECT_EQ( sessionId, same->SessionId );
		EXPECT_EQ( Jde::UserPK{7}, same->UserPK ) << "the accepted answer carries the 3rd party's user";
		Server::Sessions::Remove( sessionId );
	}

	flat_map<RequestId,string> _requests; flat_map<RequestId,string> _responses; flat_map<RequestId,string> _echoFailures; mutex _echoMutex;
	//Records the outcome either way.  A failed echo used to skip the emplace, so EchoAttack's wait on _requests.size() could never
	//be met and the suite hung instead of failing - which ql-review3 #16's message cap made routine.
	α EchoText( uint requestId, string text )->ClientSocketAwait<string>::Task{
		string response; optional<string> failure;
		try{
			response = co_await _clientSession->Echo( text );
		}
		catch( std::exception& e ){
			failure = e.what();
		}
		lg _{ _echoMutex };
		_requests.emplace( requestId, move(text) );
		if( failure )
			_echoFailures.emplace( requestId, move(*failure) );
		else
			_responses.emplace( requestId, move(response) );
	}
	//A session created over rest carries the rest timeout; the socket connecting on it must promote it to /http/socketTimeout.
	TEST_F( SocketTests, SocketPromotesSessionTimeout ){
		Stopwatch sw{ "SocketTests::SocketPromotesSessionTimeout", ELogTags::Test };
		let restTimeout = Chrono::ToDuration( Settings::FindSV("/http/timeout").value_or("PT30S") );
		let socketTimeout = Chrono::ToDuration( Settings::FindSV("/http/socketTimeout").value_or("P1D") );
		ASSERT_GT( socketTimeout, restTimeout*2 );//need an unambiguous gap to tell the two apart.

		login();
		let authorization = Ƒ( "{:x}", _sessionId );
		let expiration = [&authorization]()->TimePoint{//the mock '/timeout' target echos SessionInfo::Expiration.
			let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/timeout", Port, {.Authorization=authorization}} );
			return Chrono::ToTimePoint( Json::AsString(res.Json(), "value") );
		};

		let restStart = Chrono::ToClock<Clock,steady_clock>( steady_clock::now() );
		let restExpiration = expiration();
		DBG( "rest expiration: '{}', expected before '{}'", ToIsoString(restExpiration), ToIsoString(restStart+restTimeout+1s) );
		ASSERT_LT( restExpiration, restStart+restTimeout+1s );//no socket yet.

		connectSocket();
		let socketStart = Chrono::ToClock<Clock,steady_clock>( steady_clock::now() );
		let socketExpiration = expiration();//a rest request, so this also proves the promotion is sticky.
		DBG( "socket expiration: '{}', expected after '{}'", ToIsoString(socketExpiration), ToIsoString(socketStart+restTimeout+1s) );
		ASSERT_GT( socketExpiration, socketStart+restTimeout+1s );
		ASSERT_LE( socketExpiration, socketStart+socketTimeout+1s );
	}

	TEST_F( SocketTests, EchoAttack ){
		Stopwatch sw{ "WebTests::EchoAttack", ELogTags::Test };
		createSession();
		//ql-review3 #16 caps a socket message (Streams.cpp read_message_max), so the largest echo has to stay under it: past the
		//cap the server drops the session and every request in flight fails - OversizeMessageClosesSocket covers that.  The cap is
		///http/socketMessageMax since web-review3 #14.  The envelope is the protobuf framing around echo_text: a few tags and
		//lengths plus the request id.
		let socketMax = Server::SocketMessageMax();
		constexpr uint size = 1000;
		constexpr uint envelope = 64;
		ASSERT_GT( socketMax, envelope+size ) << "Web.Tests.jsonnet sets /http/socketMessageMax; the echoes are sized from it";
		let payloadBase = (socketMax-envelope)/size;
		string text( payloadBase*size, 'a' );
		{
			lg _{ _echoMutex };
			_requests.clear(); _responses.clear(); _echoFailures.clear();
		}
		std::this_thread::sleep_for( 1ms );
		TRACE( "----------------------------------------------------------------" );
		for( uint i=1; i<=size; ++i ){
			EchoText( i, text.substr(0,i*payloadBase) );
		}
		//Bounded, so a stranded echo fails the test instead of hanging it.  Each request already has /web/client/socketRequestTimeout
		//to be answered before the client fails it, so this only trips if that watchdog did not release one.
		let deadline = steady_clock::now()+30s;
		for( ;; ){
			{
				lg _{ _echoMutex };
				if( _requests.size()==size )
					break;
				ASSERT_LT( steady_clock::now(), deadline ) << Ƒ( "{} of {} echoes completed", _requests.size(), size );
			}
			std::this_thread::sleep_for( 10ms );
		}
		ASSERT_TRUE( _echoFailures.empty() ) << Ƒ( "{} echoes failed, first [{}]: {}", _echoFailures.size(), _echoFailures.begin()->first, _echoFailures.begin()->second );
		for( auto&& [id, text] : _requests )
			ASSERT_EQ( text, _responses[id] );
	}

	//ql-review3 #16: a socket message is capped.  Past it Beast fails the server's read with message_too_big and the session is
	//dropped, so the client sees its request fail and OnClose run - not a 413, and not a hang: EchoAttack used to send 32KB echoes
	//and wait forever for answers that could never come.  web-review3 #14 moved the cap off /http/bodyLimit onto its own setting.
	TEST_F( SocketTests, OversizeMessageClosesSocket ){
		let limit = Server::SocketMessageMax();
		ASSERT_TRUE( limit ) << "Web.Tests.jsonnet sets /http/socketMessageMax; without it there is no cap to exceed";
		createSession();
		let sessionId = _clientSession->Id();
		//extra parens: the comma in BlockAwait<A,B> would otherwise split the macro's argument list.
		EXPECT_THROW( (BlockAwait<ClientSocketAwait<string>,string>( _clientSession->Echo(string(limit+1024, 'a')) )), Exception );
		for( uint i=0; i<100 && !_clientSession->OnCloseCount(); ++i )
			std::this_thread::sleep_for( 10ms );
		EXPECT_EQ( _clientSession->OnCloseCount(), 1u );
		//and it was the cap that ended it, not the request deadline.
		vector<Logging::Entry> logs;
		for( uint i=0; i<100 && logs.empty(); ++i ){
			logs = Logging::Find( [=](const Logging::Entry& m){
				return m.Text.contains("exceeded the locally configured limit") && m.Arguments.size()==1 && m.Arguments[0]==Ƒ( "[{:x}]Server::DoRead", sessionId );
			});
			if( logs.empty() )
				std::this_thread::sleep_for( 10ms );
		}
		EXPECT_FALSE( logs.empty() ) << "the server should have refused the message as too big";
	}

	//web-review3 #6: every read error other than websocket::error::closed routes through OnDisconnect (Streams.cpp DoRead) - beast's
	//idle timeout, connection_reset, message_too_big.  The base was `{}`, so a session whose peer vanished without a close frame
	//never reached OnClose: it stayed in _socketSessions, the SocketServerListener<->session sp cycle kept it alive, and the fd
	//leaked until process stop.  AppServer's session overrode OnDisconnect, OpcGateway's GatewaySocketSession did not - the main
	//gateway product.  Driven by the size cap because that is the one non-`closed` read error this harness can produce on demand.
	TEST_F( SocketTests, DisconnectClosesServerSession ){
		let limit = Server::SocketMessageMax();//web-review3 #14 moved this off /http/bodyLimit; the point here is only that it is a non-`closed` read error.
		ASSERT_TRUE( limit ) << "Web.Tests.jsonnet sets /http/socketMessageMax; without it there is no cap to exceed";
		createSession();
		let sessionId = _clientSession->Id();
		//extra parens: the comma in BlockAwait<A,B> would otherwise split the macro's argument list.
		EXPECT_THROW( (BlockAwait<ClientSocketAwait<string>,string>( _clientSession->Echo(string(limit+1024, 'a')) )), Exception );
		//Text/Arguments, never Entry::Message(): its args are strings, so the `{:x}` here takes the format-error path, which logs
		//from inside MemoryLog::Find's lock and deadlocks (same trap BlockAwait.cpp:35 documents).
		let serverClosed = [sessionId]{//the base OnClose's own log line is the externally visible proof the session was torn down.
			return !Logging::Find( [sessionId](const Logging::Entry& m){
				return m.Arguments.size()==3 && m.Arguments[2]=="ServerSocket::OnClose." && m.Arguments[0]==Ƒ( "{}", sessionId );
			}).empty();
		};
		for( uint i=0; i<100 && !serverClosed(); ++i )
			std::this_thread::sleep_for( 10ms );
		EXPECT_TRUE( serverClosed() ) << "a read error other than a close frame must still reach OnClose - otherwise the session, its listener cycle and its fd leak";
	}

	//web-review3 #7: a failed websocket handshake left the session registered in _socketSessions, the SocketServerListener<->session
	//sp cycle intact and the fd held - Run() builds all three before DoAccept, and OnAccept's error path only logged and returned.
	//Unauthenticated: an upgrade without Sec-WebSocket-Key is enough, and a few thousand of them reach EMFILE.
	TEST_F( SocketTests, FailedAcceptClosesSession ){
		net::io_context ioc;
		tcp::resolver resolver{ ioc };
		tcp::socket socket{ ioc };
		net::connect( socket, resolver.resolve(Host, Ƒ("{}", Port)) );
		http::request<http::empty_body> req{ http::verb::get, "/", 11 };
		req.set( http::field::host, Host );
		req.set( http::field::connection, "upgrade" );
		req.set( http::field::upgrade, "websocket" );//no Sec-WebSocket-Key: is_upgrade() is still true, so a session is built, then async_accept fails with no_sec_key.
		http::write( socket, req );
		beast::error_code ec;
		beast::flat_buffer buffer;
		http::response<http::string_body> res;
		http::read( socket, buffer, res, ec );//whatever beast answers is not what is under test - only that the server let go of the session.
		socket.close( ec );

		//Text/Arguments, never Entry::Message() - see DisconnectClosesServerSession.  The id isn't knowable here, but SetUp cleared
		//the log and this test opens the only socket, so the marker alone is unambiguous.
		let serverClosed = []{
			return !Logging::Find( [](const Logging::Entry& m){
				return m.Arguments.size()==3 && m.Arguments[2]=="ServerSocket::OnClose.";
			}).empty();
		};
		for( uint i=0; i<100 && !serverClosed(); ++i )
			std::this_thread::sleep_for( 10ms );
		EXPECT_TRUE( serverClosed() ) << "a failed accept must still reach OnClose - otherwise the session, its listener cycle and its fd leak";
	}

	//web-review3 #11: a listener that cannot bind left Server::Start parked in BlockTillStarted forever - the process neither
	//exited nor threw, despite Start being declared ε.  That is the "suites hang if anything holds the port" symptom; here the
	//fixture's own server is already on Mock::Port, so a second handler on it cannot bind.
	//web-review3 #12: every accepted connection pushed an sp<cancellation_signal> into Execution's process-global list and nothing
	//ever removed it - RemoveCancelSignal existed with zero callers.  Without keep-alive each http request is a fresh accept, so a
	//gateway serving a UI grew by several MB/day and Execution::Stop walked the lot at shutdown.
	TEST_F( SocketTests, AcceptedConnectionReleasesCancelSignal ){
		let before = Execution::CancelSignalCount();
		constexpr uint requests{ 5 };
		for( uint i=0; i<requests; ++i )//each ping is its own accept: RunSession co_returns rather than handling keep-alive.
			BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host, "/ping", "", Port, {.Verb=http::verb::post}} );
		//<=, not ==: connections from earlier tests may still be draining, so the count can legitimately fall.  Growth is the bug -
		//before the fix this sat at before+5 and never came down.
		for( uint i=0; i<200 && Execution::CancelSignalCount()>before; ++i )
			std::this_thread::sleep_for( 10ms );
		EXPECT_LE( Execution::CancelSignalCount(), before ) << Ƒ( "{} accepted connections retained their cancellation_signal", Execution::CancelSignalCount()-before );
	}

	TEST_F( SocketTests, StartThrowsWhenPortIsHeld ){
		auto handler = ms<Mock::RequestHandler>( Settings::AsObject("/http") );
		//no Server::Stop on the failed handler: _socketSessions is process-global, so stopping it would tear down the fixture's
		//live server too.  The failed start leaves nothing else behind but its cancel signal (that is #12).
		EXPECT_THROW( Server::Start(handler), Exception ) << "a listener that cannot bind must fail Start, not park it";
	}

	//install-issues #56:  the check a server runs before its database, on the fixture's live listener's port and on one nothing holds.
	TEST_F( SocketTests, ThrowIfPortTaken ){
		const Server::IRequestHandler::WebServerSettings held{ Settings::AsObject("/http") };
		EXPECT_THROW( Server::ThrowIfPortTaken(held.Address(), held.Port()), Exception ) << "the fixture's listener holds this port";
		PortType free{};
		{
			net::io_context ioc;
			tcp::acceptor probe{ ioc, tcp::endpoint{net::ip::make_address(held.Address()), 0} };//the OS picks a free port, released at scope end
			free = probe.local_endpoint().port();
		}
		EXPECT_NO_THROW( Server::ThrowIfPortTaken(held.Address(), free) );
		EXPECT_NO_THROW( Server::ThrowIfPortTaken(held.Address(), free) ) << "the check released the port it bound";
	}

	TEST_F( SocketTests, BadSessionId ){
		_sessionId = Math::Random();
		EXPECT_THROW(createSession(), Exception);
	}

	TEST_F( SocketTests, CloseClientSide ){
		createSession();
		Close();
		Wait();
		let sessionId = _clientSession->Id();
		_clientSession = nullptr;
		std::this_thread::sleep_for( 1s );
		auto logs = Logging::Find( [=](const Logging::Entry& m){
			return m.Text.contains("The WebSocket stream was gracefully closed at both endpoints")
				&& m.Arguments.size()==1 && m.Arguments[0]==Ƒ( "[{:x}]Server::DoRead", sessionId );
		});
		ASSERT_TRUE( logs.size()>0 );
		std::this_thread::sleep_for( 100ms );
	}

	//C3: OnClose nulls the stream on the strand while Write/Close run on other threads.  Everything reaches it through StreamPtr()
	//now, and a second close finds null - which has to resume the await rather than dereference it or hang the caller.  TearDown
	//closes again after this, so the test also covers a third.
	TEST_F( SocketTests, CloseAfterClose ){
		createSession();
		Close();
		Wait();
		//BlockVoidAwait rather than the Close()/Wait() pair above: the second close completes synchronously now (await_ready sees a
		//closed/closing stream), and Wait()'s condition_variable has no predicate - Notify() would fire before it blocks and the
		//lost wakeup would hang the test rather than the code.  Before this, Suspend went straight at a nulled _stream.
		BlockVoidAwait( _clientSession->Close(true, SRCE_CUR) );
		_clientSession = nullptr;//TearDown's Close()/Wait() would hit the same lost wakeup.
	}

	//app-review2 #2: the server closing first must run the *whole* client teardown, not just drain _tasks.  Beast auto-replies
	//to the received close frame, so no async_close of ours ever completes and nothing else will ever call OnClose - which is
	//where _stream/_ioContext are nulled and where an app session drops its dead session and reconnects.  Draining _tasks alone
	//left the gateway wedged after every routine AppServer restart: still Connected(), never reconnecting.
	TEST_F( SocketTests, CloseServerSide ){
		createSession();
		EXPECT_THROW( (BlockAwait<ClientSocketAwait<string>,string>( _clientSession->CloseServerSide() )), Exception );
		for( uint i=0; i<100 && !_clientSession->OnCloseCount(); ++i )
			std::this_thread::sleep_for( 10ms );
		EXPECT_EQ( _clientSession->OnCloseCount(), 1u );//exactly once - firing it here as well as from an in-flight async_close would run a derived session's reconnect twice.
	}

	//web-review3 #5: revoking a session has to reach the live socket.  Sessions::Remove was a bare _sessions.erase, but every socket
	//query & subscription executes under the IWebsocketSession's *own* sp<SessionInfo>, so purgeSession/logout left the connection
	//running as the removed user until /http/socketTimeout - a day.
	TEST_F( SocketTests, RemoveClosesSocketSession ){
		createSession();
		ASSERT_EQ( 0u, _clientSession->OnCloseCount() );
		EXPECT_TRUE( Server::Sessions::Remove(_sessionId) ) << "login() minted the session, so it is there to remove";
		for( uint i=0; i<100 && !_clientSession->OnCloseCount(); ++i )
			std::this_thread::sleep_for( 10ms );
		EXPECT_EQ( 1u, _clientSession->OnCloseCount() ) << "revocation must close the socket bound to the session, not just drop the map entry";
	}

	//web-review3 #15: a request issued on a session whose stream is already gone was registered in _tasks, had its frame silently
	//dropped by Write, and then had nothing left to answer it - AddTimeout's CloseOnError found no stream either and returned, so
	//the caller waited out the process rather than the request timeout.  Reachable whenever an sp<IClientSocketSession> captured
	//before the close is used after it, e.g. IAppClient::SessionInfoAwait built from an earlier LoadSession().
	TEST_F( SocketTests, RequestOnClosedSessionFails ){
		createSession();
		Close();//client-initiated: OnClose drains _tasks and nulls _stream.
		Wait();
		//extra parens: the comma in BlockAwait<A,B> would otherwise split the macro's argument list.
		EXPECT_THROW( (BlockAwait<ClientSocketAwait<string>,string>( _clientSession->Echo("after close") )), Exception ) << "a request on a closed session must fail, not park its caller forever";
	}

	α BadTransmissionClientCall()ι->ClientSocketAwait<string>::Task{
		try{
			//C6: the server answers this with an exception carrying a requestId the client cannot match, so nothing ever resumes
			//the caller.  It used to hang here forever; /web/client/socketRequestTimeout now ends it.
			co_await _clientSession->BadTransmissionClient();
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		NOTIFY;
	}

	TEST_F( SocketTests, BadTransmissionClient ){
		createSession();
		BadTransmissionClientCall();

		let expiration = steady_clock::now() + 60s;
		vector<Logging::Entry> logs;
		let id = Crypto::CalcMd5( "[{}]Failed to process incoming exception '{}'."sv );
		while( logs.size()==0 && steady_clock::now()<expiration ){
			std::this_thread::sleep_for( 100ms );
			logs = Logging::Find( id );
		}
		TRACET( ELogTags::Test, "logs.size(): {}", logs.size() );
		ASSERT_TRUE( logs.size()>0 );
		//C6: and the stranded caller is now released rather than left waiting - the request deadline fails it.
		Wait();
		ASSERT_TRUE( _exception!=nullptr ) << "the unanswerable request never completed";
		_exception = nullptr;
		_clientSession = nullptr;//the request deadline already closed this session.
	}

	α BadTransmissionServerCall()ι->ClientSocketAwait<string>::Task{
		try{
			[[maybe_unused]] string y = co_await _clientSession->BadTransmissionServer();
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		NOTIFY;
	}
	TEST_F( SocketTests, BadTransmissionServer ){
		createSession();
		BadTransmissionServerCall();
		let expiration = steady_clock::now() + 20s;
		vector<Logging::Entry> logs;
		while( logs.size()==0 && steady_clock::now()<expiration ){
			std::this_thread::sleep_for( 100ms );
			logs = Logging::Find( Crypto::CalcMd5("MergePartialFromCodedStream returned false."sv) );//TODO send a exception to server.
		}
		ASSERT_TRUE( logs.size()>0 );
	}
}
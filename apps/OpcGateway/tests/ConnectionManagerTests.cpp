#include <open62541/client_config_default.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/tests/WaitUntil.h>
#include "utils/ClientFixture.h"
#include "utils/Subscribing.h"
#include "utils/TestUaServer.h"
#include "../src/UAClient.h"
#include "../src/async/ConnectionManager.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	namespace asio = boost::asio;
	using tcp = asio::ip::tcp;
	//The asio connection manager under the gateway's UA clients (#526):  open62541's contract for a UA_ConnectionManager, which
	//its client leans on at each step of a connect, a close and a shutdown, and the shutdown shape the executor's pool could not
	//serve.  The first two drive a bare UA_Client on the manager and the stock loop, as the gateway builds its own, with the
	//client's synchronous connect blocked inside el->run - the self-pipe wake is what ends it;  the rest go through the gateway.
	class ConnectionManagerTests : public ClientFixture{
	protected:
		static constexpr EThrowawayPort Port{ EThrowawayPort::ConnectionManager };
		Ω TearDownTestSuite()->void{
			PurgeServerCnnctn( Slug );
			for( let& slug : Slugs )
				PurgeServerCnnctn( slug );
		}
		α TearDown()ι->void override{
			UAClient::StopReconnects();//a server this suite took down leaves a chain retrying it.
			ClientFixture::TearDown();
		}
		Ω Configure( UA_ServerConfig& config )ε->void{
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, nullptr) );
			TestUaServer::Anonymous( config );
		}
		//A bare client on the manager, as UAClient::Create builds its own, with no wake:  nothing waits on it here.
		struct BareClient final{
			BareClient()ε{
				UA_ClientConfig config{};
				config.logging = &_logger;
				auto cm = AsioConnectionManager::Create( 0, ms<const function<void()>>([]{}) );
				config.eventLoop = UA_EventLoop_new_POSIX( config.logging );
				THROW_IF( !config.eventLoop, "no event loop" );
				config.eventLoop->registerEventSource( config.eventLoop, &cm->eventSource );
				UAε( UA_ClientConfig_setDefault(&config) );
				config.timeout = 10000;
				_ua = UA_Client_newWithConfig( &config );
				THROW_IF( !_ua, "no client" );
			}
			~BareClient(){ UA_Client_delete( _ua ); }
			α Stats()ι->AsioConnectionManager::Counts{ let y = AsioConnectionManager::Stats( _ua ); return y ? *y : AsioConnectionManager::Counts{}; }
			Opc::Logger _logger{};
			UA_Client* _ua{};
		};
		inline static const string Slug{ "opcTestsConnectionManager" };
		inline static const vector<string> Slugs{ "opcTestsConnectionManager1", "opcTestsConnectionManager2", "opcTestsConnectionManager3" };
		inline static const NodeId CurrentTime{ 0, UA_NS0ID_SERVER_SERVERSTATUS_CURRENTTIME };//a throwaway server's node that changes on its own.
		//Under BareClient's 10 s request timeout, which a connect that waited for a CLOSING would run out:  a refused connect costs
		//milliseconds on Linux and about 2 s an address on Windows (appClient.cpp).
		static constexpr Duration refusedBound{ 5s };
	};

	//Nothing listens:  OPENING is delivered inside openConnection and CLOSING from a later run, which the client turns into
	//BadConnectionRejected - BadConnectionClosed would say the OPENING came late, BadTimeout that the CLOSING never came.  The
	//reason is the manager's Warning.
	TEST_F( ConnectionManagerTests, ARefusedConnectIsRejectedAtOnce ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		let refusal = Ƒ( "connect 127.0.0.1:{} failed:  ", underlying(Port) );
		let warnings = [&]{ return Logging::Find( [&]( const Logging::Entry& e ){ return e.Level==ELogLevel::Warning && e.Message().contains( refusal ); } ).size(); };
		let before = warnings();
		BareClient client;
		let start = steady_clock::now();
		let sc = UA_Client_connect( client._ua, Url(Port).c_str() );
		let elapsed = duration_cast<Duration>( steady_clock::now()-start );
		INFO( "connect: {} in {}", UAException::Message(sc), Chrono::ToString(elapsed) );
		EXPECT_EQ( sc, UA_STATUSCODE_BADCONNECTIONREJECTED ) << UAException::Message( sc );
		EXPECT_LT( elapsed, refusedBound ) << Chrono::ToString( elapsed );
		let stats = client.Stats();
		EXPECT_EQ( stats.Opened, 1u );
		EXPECT_EQ( stats.Established, 0u );
		EXPECT_EQ( stats.Closings, 1u );
		EXPECT_TRUE( WaitUntil([&]{ return warnings()==before+1; }, 1s) ) << "no Warning containing '" << refusal << "'";
	}

	//An IP address connects without the resolver, so it never waits on another client's lookup - getaddrinfo holds one of the
	//pool's threads ~10 s for a nameserver that never answers;  a hostname still resolves.
	TEST_F( ConnectionManagerTests, AnAddressSkipsTheResolver ){
		for( let& [host, resolves] : array<std::pair<sv,uint>,3>{{ {"127.0.0.1", 0}, {"[::1]", 0}, {"localhost", 1} }} ){
			BareClient client;
			let sc = UA_Client_connect( client._ua, Url(Port, host).c_str() );
			EXPECT_EQ( sc, UA_STATUSCODE_BADCONNECTIONREJECTED ) << host << ":  " << UAException::Message( sc );
			let stats = client.Stats();
			EXPECT_EQ( stats.Opened, 1u ) << host;
			EXPECT_EQ( stats.Resolves, resolves ) << host;
		}
	}

	//A server that accepts and says nothing holds the synchronous connect inside el->run, blocked in its wait with no socket of the
	//loop's own to watch;  the manager's cancel is what gets the close to it when the server drops the connection.  The connect
	//status stays good past a close of an established connection, so open62541 reconnects from inside it, and the listener gone
	//by then turns that into the rejection the connect returns.
	TEST_F( ConnectionManagerTests, ASilentServersCloseEndsTheSyncConnect ){
		asio::io_context ctx;
		tcp::acceptor acceptor{ ctx, tcp::endpoint{asio::ip::address_v4::loopback(), underlying(Port)} };
		tcp::socket accepted{ ctx };
		asio::steady_timer hold{ ctx };
		acceptor.async_accept( accepted, [&]( const boost::system::error_code& ec ){
			if( ec )
				return;
			hold.expires_after( 300ms );
			hold.async_wait( [&]( const boost::system::error_code& ){//the listener goes too:  the client reconnects from inside the close, and a second silent accept would hold that until the timeout.
				boost::system::error_code ignored;
				accepted.close( ignored );
				acceptor.close( ignored );
			});
		});
		std::jthread server{ [&]{ ctx.run(); } };
		absl::Cleanup stop = [&]{ ctx.stop(); };
		BareClient client;
		let start = steady_clock::now();
		let sc = UA_Client_connect( client._ua, Url(Port).c_str() );
		let elapsed = duration_cast<Duration>( steady_clock::now()-start );
		INFO( "connect: {} in {}", UAException::Message(sc), Chrono::ToString(elapsed) );
		EXPECT_EQ( sc, UA_STATUSCODE_BADCONNECTIONREJECTED ) << UAException::Message( sc );
		EXPECT_GE( elapsed, 250ms ) << Chrono::ToString( elapsed );
		EXPECT_LT( elapsed, refusedBound ) << "the close waited out the request timeout:  " << Chrono::ToString( elapsed );
		let stats = client.Stats();
		EXPECT_EQ( stats.Established, 1u );
		EXPECT_EQ( stats.Opened, 2u );
		EXPECT_EQ( stats.Closings, stats.Opened );
	}

	//A server gone under a subscription:  every connection the client opened - the one it had, and the reconnect open62541 tries
	//from inside the close - gets its one CLOSING, and the gateway deregisters the client.
	TEST_F( ConnectionManagerTests, ARemoteCloseIsOneClosingPerOpen ){
		optional<TestUaServer> server;
		ASSERT_NO_THROW( server.emplace(Port, Configure) );
		GetConnection( Slug, Url(Port), "" );
		Attempt( Slug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_TRUE( _client );
		auto listener = ms<PushCount>();
		absl::Cleanup purge = [&]{ UAClient::PurgePending( listener ); };
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {CurrentTime}) );
		server.reset();
		ASSERT_TRUE( WaitUntil([&]{ return UAClient::Find(Slug, Credential{})!=_client; }, 10s) ) << "the client was not deregistered after its server went";
		std::this_thread::sleep_for( 500ms );
		let stats = AsioConnectionManager::Stats( _client->UAPointer() );
		ASSERT_TRUE( stats );
		EXPECT_EQ( stats->Established, 1u );
		EXPECT_EQ( stats->Closings, stats->Opened ) << "opened " << stats->Opened;
	}

	//RemoveClient's disconnect sends the CLO and closes behind it:  the server counts an aborted channel where a client closes
	//the socket with the CLO still queued.  The client's synchronous disconnect ends, and ~UAClient runs, with nothing but the
	//manager's net thread to answer it.
	TEST_F( ConnectionManagerTests, ACloseFlushesTheClo ){
		optional<TestUaServer> server;
		ASSERT_NO_THROW( server.emplace(Port, Configure) );
		GetConnection( Slug, Url(Port), "" );
		Attempt( Slug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_TRUE( _client );
		let before = UA_Server_getStatistics( server->Server() ).scs;
		EXPECT_EQ( before.currentChannelCount, 1u );
		wp<UAClient> weak = _client;
		UAClient::RemoveClient( move(_client) );
		ASSERT_TRUE( WaitUntil([&]{ return weak.expired(); }, 10s) ) << "the client never went away";
		ASSERT_TRUE( WaitUntil([&]{ return UA_Server_getStatistics(server->Server()).scs.currentChannelCount==0; }, 5s) ) << "the server still holds the channel";
		let after = UA_Server_getStatistics( server->Server() ).scs;
		EXPECT_EQ( after.channelAbortCount, before.channelAbortCount ) << "the channel was aborted, not closed";
	}

	//A subscribed client's close:  the server answers the Publishes still out after its CloseSession reply, and a chunk read before
	//the close reached a channel already CLOSING, which open62541 refused with BadInvalidState, logging an error.
	TEST_F( ConnectionManagerTests, NothingReachesTheClientAfterItsClose ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		optional<TestUaServer> server;
		ASSERT_NO_THROW( server.emplace(Port, Configure) );
		GetConnection( Slug, Url(Port), "" );
		Attempt( Slug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_TRUE( _client );
		auto listener = ms<PushCount>();
		absl::Cleanup purge = [&]{ UAClient::PurgePending( listener ); };
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {CurrentTime}) );
		let prefix = Ƒ( "[{:x}]", _client->Handle() );
		wp<UAClient> weak = _client;
		UAClient::RemoveClient( move(_client) );
		ASSERT_TRUE( WaitUntil([&]{ return weak.expired(); }, 10s) ) << "the client never went away";
		let refused = Logging::Find( [&]( const Logging::Entry& e ){ let m = e.Message(); return m.starts_with( prefix ) && m.contains( "Processing the message returned the error code" ); } );
		EXPECT_TRUE( refused.empty() ) << refused.front().Message();
	}

	//Shutdown stops every client at once, each disconnect spinning el->run on a pool thread:  with as many as the pool has threads,
	//a manager whose sockets ran on that pool could never close one.  Three against the suite's two threads.
	TEST_F( ConnectionManagerTests, SeveralClientsCloseAtOnce ){
		vector<sp<UAClient>> clients;
		for( let& slug : Slugs ){
			GetConnection( slug, OpcUrl(), "" );
			auto client = TryConnect( slug, SessionCredential() );
			ASSERT_TRUE( client ) << client.error()->what();
			clients.push_back( move(*client) );
		}
		vector<wp<UAClient>> weak{ clients.begin(), clients.end() };
		for( auto& client : clients )
			UAClient::RemoveClient( move(client) );
		clients.clear();
		ASSERT_TRUE( WaitUntil([&]{ return std::ranges::all_of( weak, [](let& w){ return w.expired(); } ); }, 10s) ) << "a client never went away";
	}
}
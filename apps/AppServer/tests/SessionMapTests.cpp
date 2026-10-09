#include "helpers.h"
#include "../src/WebServer.h"
#include "../src/ServerSocketSession.h"
#define let const auto

//the _sessions registry in WebServer.cpp: Server::Write targeting, FindConnection/FindApplications lookups, and the
//RemoveExisting host:port eviction - driven through real websocket registrations (kInstance).
namespace Jde::App::Server::Tests{

	struct SessionMapTests : ::testing::Test{
		α TearDown()->void override{
			for( auto& session : _sessions ){
				if( session )
					BlockVoidAwait( session->Close(true, SRCE_CUR) );
			}
			_sessions.clear();
		}
		α Connect()->sp<RawClientSession>{
			return _sessions.emplace_back( Tests::Connect() );
		}
		Ω Ping( str text, RequestId requestId )->FromServerTrans{
			FromServerTrans t;
			auto& m = *t.add_messages();
			m.set_request_id( requestId );
			m.set_query_result( text );//any string member the raw client can read back; nothing interprets it.
			return t;
		}
		vector<sp<RawClientSession>> _sessions;
	};

	TEST_F( SessionMapTests, FindApplications ){
		auto client = Connect();
		let pks = RegisterInstance( *client, "Tests.FindApps", "find-1", "find-host", 0, 4242 );
		EXPECT_NE( pks.Program, 0u ); EXPECT_NE( pks.Instance, 0u ); EXPECT_NE( pks.Connection, 0u );
		let instances = Server::FindApplications( "Tests.FindApps" );
		ASSERT_EQ( instances.size(), 1u );
		EXPECT_EQ( instances[0].host(), "find-host" );
		EXPECT_EQ( instances[0].instance_name(), "find-1" );
		EXPECT_EQ( instances[0].pid(), 4242u );
		EXPECT_TRUE( Server::FindApplications("Tests.NoSuchApp").empty() );
	}

	TEST_F( SessionMapTests, WriteTargetsInstance ){
		auto client = Connect();
		let pks = RegisterInstance( *client, "Tests.WriteTarget", "wt-1", "wt-host", 0 );
		let delivered = Server::Write( pks.Program, pks.Instance, Ping("ping-targeted", 777) );
		EXPECT_EQ( delivered, pks.Connection );//the pk a forward binds its response to must be the connection actually written to.
		auto m = client->WaitFor( [](let& m){ return m.value_case()==FromServerMessage::kQueryResult; } );
		ASSERT_TRUE( m );
		EXPECT_EQ( m->query_result(), "ping-targeted" );
		EXPECT_EQ( m->request_id(), 777u );
	}

	TEST_F( SessionMapTests, WriteWildcardInstance ){
		auto client = Connect();
		let pks = RegisterInstance( *client, "Tests.WriteWild", "ww-1", "ww-host", 0 );
		let delivered = Server::Write( pks.Program, nullopt, Ping("ping-wild", 778) );//instancePK nullopt = any instance of the app.
		EXPECT_EQ( delivered, pks.Connection );
		auto m = client->WaitFor( [](let& m){ return m.value_case()==FromServerMessage::kQueryResult; } );
		ASSERT_TRUE( m );
		EXPECT_EQ( m->query_result(), "ping-wild" );
	}

	TEST_F( SessionMapTests, WriteWrongInstanceThrows ){
		auto client = Connect();
		let pks = RegisterInstance( *client, "Tests.WriteWrongInst", "wwi-1", "wwi-host", 0 );
		EXPECT_THROW( Server::Write(pks.Program, pks.Instance+999, Ping("never", 1)), runtime_error );
	}

	//an unregistered session's ProgramPK is 0 - appPK 0 must not match it, else a forward addressed to "app 0" lands on
	//an arbitrary connecting socket.
	TEST_F( SessionMapTests, WriteZeroAppThrows ){
		auto unregistered = Connect();
		EXPECT_THROW( Server::Write(0, nullopt, Ping("never", 2)), runtime_error );
		EXPECT_THROW( Server::Write(0xFFFFF0, nullopt, Ping("never", 3)), runtime_error );
	}

	TEST_F( SessionMapTests, FindConnection ){
		auto client = Connect();
		let pks = RegisterInstance( *client, "Tests.FindConnection", "fc-1", "fc-host", 0 );
		auto session = Server::FindConnection( pks.Connection );
		ASSERT_TRUE( session );
		EXPECT_EQ( session->ConnectionPK(), pks.Connection );
		EXPECT_EQ( session->ProgramPK(), pks.Program );
		EXPECT_EQ( session->InstancePK(), pks.Instance );
		EXPECT_EQ( session->Instance().application(), "Tests.FindConnection" );
		EXPECT_FALSE( Server::FindConnection(0) );//0 = not-yet-registered; must never match.
		EXPECT_FALSE( Server::FindConnection(0xFFFFF1) );
	}

	//a reconnecting app re-registers with the same host:web_port - the superseded socket must be evicted and closed.
	TEST_F( SessionMapTests, RemoveExistingEvicts ){
		auto first = Connect();
		RegisterInstance( *first, "Tests.Evict", "evict-old", "evict-host", 7777 );
		auto second = Connect();
		RegisterInstance( *second, "Tests.Evict", "evict-new", "evict-host", 7777 );
		EXPECT_TRUE( first->WaitForClose() ) << "superseded socket must be closed";
		vector<Proto::FromClient::Instance> instances;
		for( let expiration = steady_clock::now()+std::chrono::seconds{5};; std::this_thread::sleep_for(std::chrono::milliseconds{50}) ){//eviction erase runs on the closing session's strand.
			instances = Server::FindApplications( "Tests.Evict" );
			if( instances.size()==1 || steady_clock::now()>expiration )
				break;
		}
		ASSERT_EQ( instances.size(), 1u );
		EXPECT_EQ( instances[0].instance_name(), "evict-new" );
	}

	//web_port 0 (a client with no /http/port, e.g. this suite) is not a unique endpoint - matching on it would evict
	//every other portless session on the host.
	TEST_F( SessionMapTests, PortlessNotEvicted ){
		auto first = Connect();
		RegisterInstance( *first, "Tests.Portless", "portless-1", "portless-host", 0 );
		auto second = Connect();
		RegisterInstance( *second, "Tests.Portless", "portless-2", "portless-host", 0 );
		std::this_thread::sleep_for( std::chrono::milliseconds{250} );//give a wrongful eviction time to land.
		EXPECT_EQ( first->CloseCount(), 0u );
		EXPECT_EQ( Server::FindApplications("Tests.Portless").size(), 2u );
	}

	//Owner ruling 2026-09-04 "issued token: renew", shape (a):  a registered app instance asking about a session (the
	//OpcServer's UAAccess renewal, the gateway's lookup) slides it, so an OPC session whose browser reaches the AppServer
	//through nothing but the gateway outlives /http/socketTimeout.  An unregistered socket gets a read only - it has not
	//proven possession of the id - and an expired session is not revived.
	Ω sessionInfo( RawClientSession& session, SessionPK id )->optional<FromServerMessage>{
		FromClientTrans t;
		auto& m = *t.add_messages();
		let requestId = session.NextRequestId();
		m.set_request_id( requestId );
		m.set_session_info( id );
		session.Write( move(t) );
		return session.WaitFor( [requestId](const FromServerMessage& m){ return m.request_id()==requestId && (m.value_case()==FromServerMessage::kSessionInfo || m.value_case()==FromServerMessage::kException); } );
	}

	TEST_F( SessionMapTests, SessionInfoSlidesForRegisteredInstance ){
		let id = MintSession();//socket-backed: NewExpiration is /http/socketTimeout, a day.
		auto info = Web::Server::Sessions::Find( id ); ASSERT_TRUE( info );
		info->Expiration = steady_clock::now()+std::chrono::minutes{1};//about to lapse.
		auto client = Connect();
		RegisterInstance( *client, "Tests.SessionSlide", "slide-1", "slide-host", 0 );
		auto reply = sessionInfo( *client, id );
		ASSERT_TRUE( reply );
		ASSERT_EQ( reply->value_case(), FromServerMessage::kSessionInfo );
		EXPECT_GT( info->Expiration, steady_clock::now()+std::chrono::hours{1} );
	}

	TEST_F( SessionMapTests, SessionInfoIsReadOnlyForUnregisteredSocket ){
		let id = MintSession();
		auto info = Web::Server::Sessions::Find( id ); ASSERT_TRUE( info );
		let soon = steady_clock::now()+std::chrono::minutes{1};
		info->Expiration = soon;
		auto client = Connect();//no kInstance - a browser, or anything else that has not registered.
		auto reply = sessionInfo( *client, id );
		ASSERT_TRUE( reply );
		EXPECT_EQ( reply->value_case(), FromServerMessage::kSessionInfo );
		EXPECT_EQ( info->Expiration, soon );
	}

	TEST_F( SessionMapTests, SessionInfoDoesNotReviveExpired ){
		let id = MintSession();
		auto info = Web::Server::Sessions::Find( id ); ASSERT_TRUE( info );
		let past = steady_clock::now()-std::chrono::minutes{1};
		info->Expiration = past;
		auto client = Connect();
		RegisterInstance( *client, "Tests.SessionSlide", "slide-2", "slide-host", 0 );
		auto reply = sessionInfo( *client, id );
		ASSERT_TRUE( reply );
		EXPECT_EQ( info->Expiration, past );
	}
}

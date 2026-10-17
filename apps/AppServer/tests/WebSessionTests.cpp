#include <absl/cleanup/cleanup.h>
#include <jde/ql/QLAwait.h>
#include "helpers.h"
#include "../src/LocalClient.h"
#include "../src/appStartup.h"
#include "../src/ql/AppServerQL.h"
#define let const auto

//the Web Sessions tab's query (#255):  sessions{…} over this process's map, session( id:… ) for one of them, and
//sessions( instanceId:… ) for a registered instance's - answered locally for this process, over the socket for another
//(InstanceSessionsAwait).
namespace Jde::App::Server::Tests{
	struct WebSessionTests : ::testing::Test{
		Ω RunQL( string query, Jde::UserPK executer={Jde::UserPK::System} )->jvalue{
			return BlockAwait<QL::QLAwait<jvalue>,jvalue>( QL::QLAwait<jvalue>{move(query), {}, executer, Server::QLPtr()} );
		}
		Ω Endpoints( const jvalue& sessions )->flat_set<string>{
			flat_set<string> y;
			for( let& s : sessions.as_array() )
				y.emplace( Json::AsString(s.as_object(), "endpoint") );
			return y;
		}
	};

	TEST_F( WebSessionTests, ListsTheLiveSessions ){
		let session = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.1", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		let y = RunQL( "sessions{ loginName domain endpoint lastUpdate expiration }" );//loginName/domain:  the users lookup, which read the raw answer as a wrapped one until #255.
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		EXPECT_TRUE( Endpoints(y).contains("10.255.0.1") ) << serialize( y );
		for( let& s : y.as_array() ){
			let& o = s.as_object();
			EXPECT_TRUE( o.contains("lastUpdate") && o.contains("expiration") ) << serialize( o );
		}
	}

	//the spa confirms its sign-in with session( id:<its own> ):  a signed-in user's own session needs no grant.
	TEST_F( WebSessionTests, OwnSessionById ){
		let session = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.2", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		let y = RunQL( Ƒ("session( id:\"{:x}\" ){{ endpoint }}", session->SessionId), Jde::UserPK{1} );
		EXPECT_EQ( Json::AsString(y.as_object(), "endpoint"), "10.255.0.2" ) << serialize( y );
	}

	//this process's own instance answers through the local ql - the same list.
	TEST_F( WebSessionTests, OwnInstanceAnswersLocally ){
		let instanceId = Server::AppClient()->InstancePK();
		ASSERT_TRUE( instanceId ) << "AppStartup registers this process; without its pk there is nothing to ask";
		let session = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.3", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		let y = RunQL( Ƒ("sessions( instanceId:{} ){{ endpoint }}", instanceId) );
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		EXPECT_TRUE( Endpoints(y).contains("10.255.0.3") ) << serialize( y );
	}

	//another instance is asked over its socket - the same query less the instanceId - and its answer is the result.
	TEST_F( WebSessionTests, ForwardsToTheInstance ){
		auto client = Connect();
		let instanceId = RegisterInstance( *client, "Tests.WebSessions", "forwarded", "websession-host", 0 ).Instance;
		jvalue y; string failure;
		std::thread query{ [&]{ try{ y = RunQL( Ƒ("sessions( instanceId:{} ){{ endpoint }}", instanceId) ); }catch( const std::exception& e ){ failure = e.what(); } } };//RunQL blocks on the answer below.
		let asked = client->WaitFor( [](const FromServerMessage& m){ return m.value_case()==FromServerMessage::kClientQuery && m.client_query().query().contains("sessions"); } );
		if( asked ){
			EXPECT_EQ( asked->client_query().query().find("instanceId"), string::npos ) << asked->client_query().query();
			FromClientTrans t;
			auto& m = *t.add_messages();
			m.set_request_id( asked->request_id() );
			m.set_query_result( serialize(jarray{ jobject{{"endpoint", "10.255.0.4"}} }) );
			client->Write( move(t) );
		}
		query.join();//unconditional - the query times out on its own when nothing answered.
		ASSERT_TRUE( asked ) << "the instance was never asked:  " << failure;
		EXPECT_TRUE( failure.empty() ) << failure;
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		EXPECT_TRUE( Endpoints(y).contains("10.255.0.4") ) << serialize( y );
		BlockVoidAwait( client->Close(true, SRCE_CUR) );
	}

	//an instance with no socket has no map to ask:  the query fails rather than answering an empty list for a live instance.
	TEST_F( WebSessionTests, ADisconnectedInstanceFails ){
		let [program, instance, connection] = App::AddConnection( "Tests.WebSessions", "offline", "websession-host", 116 );
		EXPECT_THROW( RunQL(Ƒ("sessions( instanceId:{} ){{ endpoint }}", instance)), std::runtime_error );
	}
}
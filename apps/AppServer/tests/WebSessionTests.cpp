#include <absl/cleanup/cleanup.h>
#include <jde/db/meta/AppSchema.h>
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
		Ω Status( function<void()> f )->EHttpStatus{
			try{
				f();
			}
			catch( const Exception& e ){
				return e.HttpStatus();
			}
			return EHttpStatus::None;
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

	//#255 review #7:  the users lookup asks for the signed-in users only;  one it does not name answers loginName:null, so the tab
	//can tell it from an anonymous session, which carries no loginName at all.
	TEST_F( WebSessionTests, AnUnnamedUserIsNotAnonymous ){
		let anonymous = Web::Server::Sessions::Add( Jde::UserPK{}, "10.255.0.10", false );
		let unnamed = Web::Server::Sessions::Add( Jde::UserPK{999'999}, "10.255.0.11", false );//no such user
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( anonymous->SessionId ); Web::Server::Sessions::Remove( unnamed->SessionId ); } };
		let y = RunQL( "sessions{ loginName endpoint }" );
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		let row = [&]( sv endpoint )->const jobject* {
			for( let& s : y.as_array() ){
				if( Json::AsString(s.as_object(), "endpoint")==endpoint )
					return &s.as_object();
			}
			return nullptr;
		};
		let a = row( "10.255.0.10" ); let u = row( "10.255.0.11" );
		ASSERT_TRUE( a && u ) << serialize( y );
		EXPECT_FALSE( a->contains("loginName") ) << serialize( *a );
		ASSERT_TRUE( u->contains("loginName") ) << serialize( *u );
		EXPECT_TRUE( u->at("loginName").is_null() ) << serialize( *u );
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
		absl::Cleanup close{ [&]{ BlockVoidAwait( client->Close(true, SRCE_CUR) ); } };//an ASSERT below would leave the instance registered for later tests.
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
	}

	//a gateway's logout purges the session here (HttpRequestAwait::Logout).  It never reached SessionGraphQL's PurgeBefore hook:
	//MutationAwait refused a purge of a table with no view first, so the AppServer's session outlived the logout.
	TEST_F( WebSessionTests, PurgeRemovesTheSession ){
		let session = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.12", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		let y = RunQL( Ƒ("purgeSession( id:\"{:x}\" )", session->SessionId) );
		EXPECT_FALSE( Web::Server::Sessions::Find(session->SessionId) ) << serialize( y );
	}

	//364-sessions-resource review #2:  a session a socket asks for on its user's behalf - a stand-alone gateway's AddSession - records
	//the socket's account, which may then end it without a grant (SessionGraphQL::Purge).
	TEST_F( WebSessionTests, AddSessionRecordsWhoMintedIt ){
		auto client = Connect();
		absl::Cleanup close{ [&]{ BlockVoidAwait( client->Close(true, SRCE_CUR) ); } };
		const Jde::UserPK gateway{ 1 };
		RegisterInstance( *client, "Tests.WebSessions", "minting", "websession-host", 0, 1234, "", gateway );//the socket adopts gateway's session
		FromClientTrans t;
		auto& m = *t.add_messages();
		let requestId = client->NextRequestId();
		m.set_request_id( requestId );
		auto& add = *m.mutable_add_session();
		add.set_login_name( "webSessionMinted" );
		add.set_provider_pk( underlying(Access::EProviderType::Google) );
		add.set_user_endpoint( "10.255.0.19" );
		client->Write( move(t) );
		let reply = client->WaitFor( [requestId](const FromServerMessage& m){ return m.request_id()==requestId && m.value_case()==FromServerMessage::kSessionInfo; } );
		ASSERT_TRUE( reply ) << "no SessionInfo for the AddSession";
		let session = Web::Server::Sessions::Find( reply->session_info().session_id() );
		absl::Cleanup remove{ [&]{ if( session ) Web::Server::Sessions::Remove( session->SessionId ); } };
		ASSERT_TRUE( session );
		EXPECT_NE( session->UserPK.Value, gateway.Value ) << "the session is its user's";
		EXPECT_EQ( session->MintedBy.Value, gateway.Value ) << "and the socket's account began it";
	}

	//an instance with no socket has no map to ask:  the query fails rather than answering an empty list for a live instance.
	TEST_F( WebSessionTests, ADisconnectedInstanceFails ){
		let [program, instance, connection] = App::AddConnection( "Tests.WebSessions", "offline", "websession-host", 116 );
		absl::Cleanup end{ [&]{ App::EndConnection( connection ); } };//not left live for the tests that list connections.
		EXPECT_EQ( Status([&]{ RunQL(Ƒ("sessions( instanceId:{} ){{ endpoint }}", instance)); }), EHttpStatus::NotFound );
	}

	//#255 review #4:  the singular was not InstanceSessionsAwait's, so the hook answered from this process's map with the instanceId ignored.
	TEST_F( WebSessionTests, OneSessionTakesNoInstance ){
		let session = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.9", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		let instanceId = Server::AppClient()->InstancePK();
		EXPECT_EQ( Status([&]{ RunQL(Ƒ("session( instanceId:{}, id:\"{:x}\" ){{ endpoint }}", instanceId, session->SessionId)); }), EHttpStatus::BadRequest );
	}

	//The gate, enforced:  the app schema's `sessions` (#364) - Read lists, Administer purges.  Enforced for the suite, then left as it
	//was found and the suite's grants purged (364-sessions-resource review #9):  a persistent dev database keeps both.
	struct WebSessionGateTests : WebSessionTests{
		static constexpr sv OtherSchema{ "webSessionTests" };
		Ω Id( const jvalue& v )->uint{ return Json::AsNumber<uint>( Json::AsObject(v), "id" ); }
		Ω User( str slug )->Jde::UserPK{
			auto user = RunQL( Ƒ("user( slug:\"{}\", deleted:null ){{ id }}", slug) );
			if( user.is_null() || (user.is_object() && user.get_object().empty()) )
				user = RunQL( Ƒ("createUser( slug:\"{0}\", name:\"{0}\" ){{ id }}", slug) );
			return Jde::UserPK{ (Jde::UserPK::Type)Id(user) };
		}
		Ω Enforced( str schema )->bool{
			let rows = Json::AsArray( RunQL(Ƒ("resources( schemaName:\"{}\", slug:\"sessions\", criteria:null ){{ deleted }}", schema)) );
			return rows.size() && rows[0].as_object().at("deleted").is_null();
		}
		//schema's `sessions` row, enforced or not - restore/delete only on a change:  a no-op update publishes nothing.
		Ω Enforce( str schema, bool on )->void{
			for( let& r : Json::AsArray(RunQL(Ƒ("resources( schemaName:\"{}\", slug:\"sessions\", criteria:null ){{ id deleted }}", schema))) ){
				if( r.as_object().at("deleted").is_null()!=on )
					RunQL( Ƒ("{}Resource( id:{} )", on ? "restore" : "delete", Id(r)) );
			}
		}
		Ω PurgeOthers()->void{
			for( let& r : Json::AsArray(RunQL(Ƒ("resources( schemaName:[\"{}\"] ){{ id }}", OtherSchema))) )
				RunQL( Ƒ("purgeResource( id:{} )", Id(r)) );
		}
		Ω SetUpTestSuite()->void{
			let schema = AppSchema()->Name;
			ASSERT_EQ( Json::AsArray(RunQL(Ƒ("resources( schemaName:\"{}\", slug:\"sessions\", criteria:null ){{ id }}", schema))).size(), 1u ) << "app-meta.jsonnet declares it, so the sync makes it";
			PurgeOthers();
			_stranger = User( "webSessionStranger" );
			_reader = User( "webSessionReader" );
			_admin = User( "webSessionAdmin" );
			let grant = [&]( Jde::UserPK user, Access::ERights rights ){
				let y = RunQL( Ƒ("createAcl( identity:{{ id:{} }}, permissionRight:{{ allowed:{}, denied:0, resource:{{ schemaName:\"{}\", slug:\"sessions\" }} }} ){{ permissionRight{{id}} }}", user.Value, underlying(rights), schema) );
				_grants.emplace_back( user, Json::AsNumber<uint>(Json::AsObject(y), "permissionRight/id") );
			};
			grant( _reader, Access::ERights::Read );
			grant( _admin, Access::ERights::Read | Access::ERights::Administer );
			_wasEnforced = Enforced( schema );
			Enforce( schema, true );
		}
		Ω TearDownTestSuite()->void{
			Enforce( AppSchema()->Name, _wasEnforced );
			for( let& [user, permission] : _grants )
				RunQL( Ƒ("purgeAcl( identity:{{ id:{} }}, permissionRight:{{ id:{} }} )", user.Value, permission) );
			_grants.clear();
			PurgeOthers();
		}
		static inline Jde::UserPK _stranger, _reader, _admin;
		static inline vector<std::pair<Jde::UserPK,uint>> _grants;
		static inline bool _wasEnforced{};
	};

	TEST_F( WebSessionGateTests, TheListingTakesRead ){
		let session = Web::Server::Sessions::Add( _stranger, "10.255.0.6", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		constexpr sv listing{ "sessions{ endpoint }" };
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, _stranger); }), EHttpStatus::Forbidden ) << "a session of their own does not open the listing";
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, {}); }), EHttpStatus::Unauthorized );
		jvalue y;
		EXPECT_EQ( Status([&]{ y = RunQL(string{listing}, _reader); }), EHttpStatus::None );
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		EXPECT_TRUE( Endpoints(y).contains("10.255.0.6") ) << serialize( y );

		let instanceId = Server::AppClient()->InstancePK();
		ASSERT_TRUE( instanceId );
		let forwarded = Ƒ( "sessions( instanceId:{} ){{ endpoint }}", instanceId );//InstanceSessionsAwait's own gate, before the local leg runs as the system user.
		EXPECT_EQ( Status([&]{ RunQL(forwarded, _stranger); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(forwarded, _reader); }), EHttpStatus::None );
	}

	TEST_F( WebSessionGateTests, OnlyTheirOwnSessionIsExempt ){
		let own = Web::Server::Sessions::Add( _stranger, "10.255.0.7", false );
		let other = Web::Server::Sessions::Add( _admin, "10.255.0.8", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( own->SessionId ); Web::Server::Sessions::Remove( other->SessionId ); } };
		let byId = []( SessionPK id ){ return Ƒ( "session( id:\"{:x}\" ){{ endpoint }}", id ); };
		jvalue y;
		EXPECT_EQ( Status([&]{ y = RunQL(byId(own->SessionId), _stranger); }), EHttpStatus::None );
		EXPECT_EQ( Json::AsString(y.as_object(), "endpoint"), "10.255.0.7" ) << serialize( y );
		EXPECT_EQ( Status([&]{ RunQL(byId(other->SessionId), _stranger); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(byId(0xdeadbeef), _stranger); }), EHttpStatus::Forbidden ) << "nobody's, refused alike";
		EXPECT_EQ( Status([&]{ RunQL(byId(own->SessionId), {}); }), EHttpStatus::Unauthorized ) << "the exemption is a signed-in caller's";
		EXPECT_EQ( Status([&]{ RunQL(byId(other->SessionId), _reader); }), EHttpStatus::None ) << "another's takes the listing's Read";
	}

	TEST_F( WebSessionGateTests, ThePurgeTakesAdminister ){
		let session = Web::Server::Sessions::Add( _admin, "10.255.0.13", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		let purge = Ƒ( "purgeSession( id:\"{:x}\" )", session->SessionId );
		EXPECT_EQ( Status([&]{ RunQL(purge, _stranger); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(purge, _reader); }), EHttpStatus::Forbidden ) << "Read lists, it does not purge";
		EXPECT_TRUE( Web::Server::Sessions::Find(session->SessionId) );
		EXPECT_EQ( Status([&]{ RunQL(purge, _admin); }), EHttpStatus::None );
		EXPECT_FALSE( Web::Server::Sessions::Find(session->SessionId) );
	}

	//364-sessions-resource review #2:  ending your own session, or one you signed its user in to - a stand-alone gateway's sign-out -
	//needs no grant.  Anyone else's still takes Administer.
	TEST_F( WebSessionGateTests, TheirOwnAndTheOnesTheyMintedNeedNoGrant ){
		let own = Web::Server::Sessions::Add( _stranger, "10.255.0.17", false );
		let minted = Web::Server::Sessions::Add( _admin, "10.255.0.18", false, _stranger );//as AddSession records the gateway's account
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( own->SessionId ); Web::Server::Sessions::Remove( minted->SessionId ); } };
		let purge = []( SessionPK id ){ return Ƒ( "purgeSession( id:\"{:x}\" )", id ); };
		EXPECT_EQ( Status([&]{ RunQL(purge(minted->SessionId), _reader); }), EHttpStatus::Forbidden ) << "neither theirs nor begun by them";
		EXPECT_EQ( Status([&]{ RunQL(purge(own->SessionId), {}); }), EHttpStatus::Unauthorized ) << "the exemption is a signed-in caller's";
		EXPECT_EQ( Status([&]{ RunQL(purge(own->SessionId), _stranger); }), EHttpStatus::None );
		EXPECT_FALSE( Web::Server::Sessions::Find(own->SessionId) );
		EXPECT_EQ( Status([&]{ RunQL(purge(minted->SessionId), _stranger); }), EHttpStatus::None ) << "the account that signed its user in";
		EXPECT_FALSE( Web::Server::Sessions::Find(minted->SessionId) );
		EXPECT_EQ( Status([&]{ RunQL(purge(0xdeadbeef), _stranger); }), EHttpStatus::Forbidden ) << "nobody's, refused as the listing refuses it";
	}

	//#364:  the hub holds the gateway's `sessions` beside the app schema's, and the gate that asked by slug alone answered with
	//either - enforcing the gateway's locked the listing out.  Another schema's `sessions`, enforced and granting nothing, gates nothing here.
	TEST_F( WebSessionGateTests, AnotherSchemasSessionsDoNotGate ){
		RunQL( Ƒ("createResource( schemaName:\"{}\", name:\"sessions\", slug:\"sessions\", allowed:{} ){{ id }}", OtherSchema, underlying(Access::ERights::Read | Access::ERights::Administer)) );
		Enforce( string{OtherSchema}, true );
		absl::Cleanup purge{ []{ PurgeOthers(); } };
		let session = Web::Server::Sessions::Add( _stranger, "10.255.0.15", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		constexpr sv listing{ "sessions{ endpoint }" };
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, _reader); }), EHttpStatus::None ) << "the reader's grant is on app's, and app's answers";

		let app = AppSchema()->Name;
		Enforce( app, false );
		absl::Cleanup enforce{ [&]{ Enforce( app, true ); } };
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, _stranger); }), EHttpStatus::None ) << "app's unenforced:  open, whatever the other schema enforces";
	}
}
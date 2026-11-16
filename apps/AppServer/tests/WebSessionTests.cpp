#include <absl/cleanup/cleanup.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/ql/QLAwait.h>
#include <jde/web/Jwt.h>
#include <jde/tests/Capture.h>
#include <jde/tests/HttpTest.h>
#include "helpers.h"
#include "../src/Guest.h"
#include "../src/LocalClient.h"
#include "../src/appStartup.h"
#include "../src/ql/AppServerQL.h"
#define let const auto

//the Web Sessions tab's query (#255):  sessions{…} over this process's map, session( id:… ) for one of them, and
//sessions( instanceId:… ) for a registered instance's - answered locally for this process, over the socket for another
//(InstanceSessionsAwait).
namespace Jde::App::Server::Tests{
	struct WebSessionTests : ::testing::Test{
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

	//an expired session stays in the map until UpdateExpiration's lazy trim, up to a RestSessionTimeout later - it is not listed.
	TEST_F( WebSessionTests, ExpiredSessionsAreNotListed ){
		let live = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.20", false );
		let expired = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.21", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( live->SessionId ); Web::Server::Sessions::Remove( expired->SessionId ); } };
		expired->Expiration = steady_clock::now()-1min;
		let y = RunQL( "sessions{ endpoint }" );
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		EXPECT_TRUE( Endpoints(y).contains("10.255.0.20") ) << serialize( y );
		EXPECT_FALSE( Endpoints(y).contains("10.255.0.21") ) << serialize( y );
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
		let y = RunQL( Ƒ("session( id:\"{:x}\" ){{ endpoint }}", session->SessionId), {}, Jde::UserPK{1} );
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
		absl::Cleanup close{ [&]{ BlockAwait( client->Close(true, SRCE_CUR) ); } };//an ASSERT below would leave the instance registered for later tests.
		let instanceId = RegisterInstance( *client, "Tests.WebSessions", "forwarded", "websession-host", 0 ).Instance;
		let y = AskInstance( *client, Ƒ("sessions( instanceId:{} ){{ endpoint }}", instanceId), "sessions", [&](let& asked){
			EXPECT_EQ( asked.client_query().query().find("instanceId"), string::npos ) << asked.client_query().query();
			Send( *client, asked.request_id(), [](auto& m){ m.set_query_result( serialize(jarray{ jobject{{"endpoint", "10.255.0.4"}} }) ); } );
		});
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
		absl::Cleanup close{ [&]{ BlockAwait( client->Close(true, SRCE_CUR) ); } };
		const Jde::UserPK gateway{ 1 };
		RegisterInstance( *client, "Tests.WebSessions", "minting", "websession-host", 0, 1234, "", gateway );//the socket adopts gateway's session
		let requestId = Send( *client, [](auto& m){
			auto& add = *m.mutable_add_session();
			add.set_login_name( "webSessionMinted" );
			add.set_provider_pk( underlying(Access::EProviderType::Google) );
			add.set_user_endpoint( "10.255.0.19" );
		});
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
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, {}, _stranger); }), EHttpStatus::Forbidden ) << "a session of their own does not open the listing";
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, {}, {}); }), EHttpStatus::Unauthorized );
		jvalue y;
		EXPECT_EQ( Status([&]{ y = RunQL(string{listing}, {}, _reader); }), EHttpStatus::None );
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		EXPECT_TRUE( Endpoints(y).contains("10.255.0.6") ) << serialize( y );

		let instanceId = Server::AppClient()->InstancePK();
		ASSERT_TRUE( instanceId );
		let forwarded = Ƒ( "sessions( instanceId:{} ){{ endpoint }}", instanceId );//InstanceSessionsAwait's own gate, before the local leg runs as the system user.
		EXPECT_EQ( Status([&]{ RunQL(forwarded, {}, _stranger); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(forwarded, {}, _reader); }), EHttpStatus::None );
	}

	TEST_F( WebSessionGateTests, OnlyTheirOwnSessionIsExempt ){
		let own = Web::Server::Sessions::Add( _stranger, "10.255.0.7", false );
		let other = Web::Server::Sessions::Add( _admin, "10.255.0.8", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( own->SessionId ); Web::Server::Sessions::Remove( other->SessionId ); } };
		let byId = []( SessionPK id ){ return Ƒ( "session( id:\"{:x}\" ){{ endpoint }}", id ); };
		jvalue y;
		EXPECT_EQ( Status([&]{ y = RunQL(byId(own->SessionId), {}, _stranger); }), EHttpStatus::None );
		EXPECT_EQ( Json::AsString(y.as_object(), "endpoint"), "10.255.0.7" ) << serialize( y );
		EXPECT_EQ( Status([&]{ RunQL(byId(other->SessionId), {}, _stranger); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(byId(0xdeadbeef), {}, _stranger); }), EHttpStatus::Forbidden ) << "nobody's, refused alike";
		EXPECT_EQ( Status([&]{ RunQL(byId(own->SessionId), {}, {}); }), EHttpStatus::Unauthorized ) << "the exemption is a signed-in caller's";
		EXPECT_EQ( Status([&]{ RunQL(byId(other->SessionId), {}, _reader); }), EHttpStatus::None ) << "another's takes the listing's Read";
	}

	TEST_F( WebSessionGateTests, ThePurgeTakesAdminister ){
		let session = Web::Server::Sessions::Add( _admin, "10.255.0.13", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		let purge = Ƒ( "purgeSession( id:\"{:x}\" )", session->SessionId );
		EXPECT_EQ( Status([&]{ RunQL(purge, {}, _stranger); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(purge, {}, _reader); }), EHttpStatus::Forbidden ) << "Read lists, it does not purge";
		EXPECT_TRUE( Web::Server::Sessions::Find(session->SessionId) );
		EXPECT_EQ( Status([&]{ RunQL(purge, {}, _admin); }), EHttpStatus::None );
		EXPECT_FALSE( Web::Server::Sessions::Find(session->SessionId) );
	}

	//364-sessions-resource review #2:  ending your own session, or one you signed its user in to - a stand-alone gateway's sign-out -
	//needs no grant.  Anyone else's still takes Administer.
	TEST_F( WebSessionGateTests, TheirOwnAndTheOnesTheyMintedNeedNoGrant ){
		let own = Web::Server::Sessions::Add( _stranger, "10.255.0.17", false );
		let minted = Web::Server::Sessions::Add( _admin, "10.255.0.18", false, _stranger );//as AddSession records the gateway's account
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( own->SessionId ); Web::Server::Sessions::Remove( minted->SessionId ); } };
		let purge = []( SessionPK id ){ return Ƒ( "purgeSession( id:\"{:x}\" )", id ); };
		EXPECT_EQ( Status([&]{ RunQL(purge(minted->SessionId), {}, _reader); }), EHttpStatus::Forbidden ) << "neither theirs nor begun by them";
		EXPECT_EQ( Status([&]{ RunQL(purge(own->SessionId), {}, {}); }), EHttpStatus::Unauthorized ) << "the exemption is a signed-in caller's";
		EXPECT_EQ( Status([&]{ RunQL(purge(own->SessionId), {}, _stranger); }), EHttpStatus::None );
		EXPECT_FALSE( Web::Server::Sessions::Find(own->SessionId) );
		EXPECT_EQ( Status([&]{ RunQL(purge(minted->SessionId), {}, _stranger); }), EHttpStatus::None ) << "the account that signed its user in";
		EXPECT_FALSE( Web::Server::Sessions::Find(minted->SessionId) );
		EXPECT_EQ( Status([&]{ RunQL(purge(0xdeadbeef), {}, _stranger); }), EHttpStatus::Forbidden ) << "nobody's, refused as the listing refuses it";
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
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, {}, _reader); }), EHttpStatus::None ) << "the reader's grant is on app's, and app's answers";

		let app = AppSchema()->Name;
		Enforce( app, false );
		absl::Cleanup enforce{ [&]{ Enforce( app, true ); } };
		EXPECT_EQ( Status([&]{ RunQL(string{listing}, {}, _stranger); }), EHttpStatus::None ) << "app's unenforced:  open, whatever the other schema enforces";
	}

	//#483, the guest:  /access/guest names a user by slug, `webSessionGuest` here, and a session minted for a caller with no
	//Authorization is that user's.  The suite creates the user - the slug is unresolved at a start, the seed's case (plan Phase 8) -
	//and purges it at its end, which clears the guest, so the suite's anonymous cases hold whichever order the fixtures run in.
	struct WebGuestTests : HttpTest{
		Ω Slug()->string{ return Settings::FindString( "/access/guest" ).value_or( "" ); }
		Ω Purge()->void{
			for( let& r : Json::AsArray(RunQL(Ƒ("users( slug:\"{}\" ){{ id }}", Slug()))) )//deleted or not
				RunQL( Ƒ("purgeUser( id:{} )", Id(r)) );
		}
		Ω GuestPK()->Jde::UserPK{ return Web::Server::Sessions::Guest(); }
		//the session a request with no Authorization is minted, from the answer's header.
		using HttpTest::Minted;
		Ω Minted( string target="/opcGateways" )->SessionPK{
			let id = Minted( Get(Port(), move(target)) );
			THROW_IF( !id, "The answer named no session." );
			return *id;
		}
		Ω UserOf( SessionPK id )->Jde::UserPK{ let session = Web::Server::Sessions::Find( id ); return session ? session->UserPK : Jde::UserPK{ Jde::UserPK::System }; }
		//Read on app/sessions for the guest:  the permission's id, for Revoke.
		Ω GrantSessionsRead( str schema )->uint{
			let grant = RunQL( Ƒ("createAcl( identity:{{ id:{} }}, permissionRight:{{ allowed:{}, denied:0, resource:{{ schemaName:\"{}\", slug:\"sessions\" }} }} ){{ permissionRight{{id}} }}", _user.Value, underlying(Access::ERights::Read), schema) );
			return Json::AsNumber<uint>( Json::AsObject(grant), "permissionRight/id" );
		}
		Ω Revoke( uint permission )->void{ RunQL( Ƒ("purgeAcl( identity:{{ id:{} }}, permissionRight:{{ id:{} }} )", _user.Value, permission) ); }
		Ω Warned( const Logging::Capture& warnings, sv text )->bool{ return !warnings.Find( [text](let& e){ return e.Level==ELogLevel::Warning && e.Message().contains( text ); } ).empty(); }
		Ω SetUpTestSuite()->void{
			ASSERT_FALSE( Slug().empty() ) << "the suite's config names the guest";
			Purge();
			_user = User( Slug() );//the listener's userCreated sets the guest, on the mutation's thread, before the create returns.
			ASSERT_EQ( GuestPK().Value, _user.Value ) << "the guest wasn't resolved";
		}
		Ω TearDownTestSuite()->void{
			Purge();
			EXPECT_FALSE( GuestPK() ) << "the purge cleared the guest";
		}
		static inline Jde::UserPK _user;
	};

	//unset - as shipped - a session minted with no sign-in is nobody's, and a write on a rights table is refused as
	//HttpRoutingTests.AnonymousMutationOnARightsTableIsRefused has it.
	TEST_F( WebGuestTests, UnsetTheSessionIsNobodys ){
		Web::Server::Sessions::SetGuest( {} );
		absl::Cleanup restore{ []{ Web::Server::Sessions::SetGuest( _user ); } };
		let id = Minted();
		absl::Cleanup remove{ [id]{ Web::Server::Sessions::Remove( id ); } };
		EXPECT_FALSE( UserOf(id) );
		let body = serialize( jobject{{"query", "mutation createIdentity( name:\"guest-unset\", slug:\"guest-unset\" ){ id }"}} );
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/graphql", body, Ƒ("{:x}", id)); }), http::status::unauthorized );
		EXPECT_TRUE( Json::AsArray(RunQL("identities( slug:\"guest-unset\" ){ id }")).empty() ) << "the identity was created anyway";
	}

	//set, the session is the guest's:  /serverSettings' `active` and /opcGateways' full answer, the readers of "has a user", say
	//yes with no change - and a page's first call, /serverSettings with no header, is minted its session and told it, so the page
	//shows Guest from its first load.
	TEST_F( WebGuestTests, SetTheSessionIsTheGuests ){
		let id = Minted();
		absl::Cleanup remove{ [id]{ Web::Server::Sessions::Remove( id ); } };
		EXPECT_EQ( UserOf(id).Value, _user.Value );
		EXPECT_TRUE( Json::AsBool(Get(Port(), "/serverSettings", Ƒ("{:x}", id)).Json(), "active") );

		let first = Get( Port(), "/serverSettings" );
		let firstId = Minted( first );
		ASSERT_TRUE( firstId ) << "a page's first call was told no session";
		absl::Cleanup removeFirst{ [&]{ Web::Server::Sessions::Remove( *firstId ); } };
		EXPECT_EQ( UserOf(*firstId).Value, _user.Value );
		EXPECT_TRUE( Json::AsBool(first.Json(), "active") );

		auto client = Connect();
		absl::Cleanup close{ [&]{ BlockAwait( client->Close(true, SRCE_CUR) ); } };
		RegisterInstance( *client, "Jde.OpcGateway", "guest-gw", "gw-host", 4323, 4343 );
		let answer = Get( Port(), "/opcGateways" ).Json();
		for( let& jserver : Json::AsArray(answer, "servers") ){
			if( let& o = jserver.as_object(); Json::AsString(o, "instanceName")=="guest-gw" )
				EXPECT_TRUE( o.contains("pid") && o.contains("application") ) << "a guest is a caller with a user:  " << serialize( o );
		}
	}

	//the rights are the guest's own, through FindUserLocked:  with app/sessions enforced, its listing is refused until the guest holds Read.
	TEST_F( WebGuestTests, TheRightsAreTheGuestsOwn ){
		let schema = AppSchema()->Name;
		let wasEnforced = WebSessionGateTests::Enforced( schema );
		WebSessionGateTests::Enforce( schema, true );
		absl::Cleanup restore{ [&]{ WebSessionGateTests::Enforce( schema, wasEnforced ); } };
		let id = Minted();
		absl::Cleanup remove{ [id]{ Web::Server::Sessions::Remove( id ); } };
		let authorization = Ƒ( "{:x}", id );
		let listing = serialize( jobject{{"query", "sessions{ endpoint }"}} );
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/graphql", listing, authorization); }), http::status::forbidden ) << "a user with no grant";
		let permission = GrantSessionsRead( schema );
		absl::Cleanup revoke{ [&]{ Revoke( permission ); } };
		let sessions = QL( Port(), "sessions{ endpoint }", authorization );
		EXPECT_TRUE( sessions.is_object() && sessions.get_object().contains("sessions") ) << serialize( sessions );
	}

	//a sign-in mints a session of its own, as it does today - the jwt's user, not the guest.  The jwt is a certificate login's, this
	//process's own web certificate (appClient.cpp getJwt), which the suite's /access/trustedCertDirs anchors.
	TEST_F( WebGuestTests, ASignInReplacesTheGuest ){
		Crypto::CryptoSettings ssl{ Json::FindDefaultObject(Settings::AsObject("/http/app"), "ssl") };
		auto certificate = Crypto::ReadCertificate( ssl.Certificate.Path );
		const Crypto::Certificate info{ certificate };
		Web::Jwt jwt{ {}, {0}, info.CommonName, info.CommonName, 0, {}, TimePoint::min(), "WebGuestTests", ssl.PrivateKey, move(certificate) };
		let res = Post( Port(), "/login", "{}", "Bearer "+jwt.Payload() );
		let id = Minted( res );
		ASSERT_TRUE( id ) << "the login named no session";
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( *id ); } };
		let user = UserOf( *id );
		EXPECT_TRUE( user && user.Value!=Jde::UserPK::System );
		EXPECT_NE( user.Value, _user.Value ) << "the guest";
	}

	//the guest's user deleted:  the next session with no sign-in is nobody's, with the Warning;  restored by id, which names no slug,
	//the guest again, after a start that read it deleted too.  A session minted before keeps its user, refused on an enforced resource as any deleted user is (FindUserLocked):
	//with app/sessions enforced and granted the guest, its listing passes before the delete and is refused after it.
	TEST_F( WebGuestTests, TheUserDeletedMakesTheNextSessionAnonymous ){
		let schema = AppSchema()->Name;
		let wasEnforced = WebSessionGateTests::Enforced( schema );
		WebSessionGateTests::Enforce( schema, true );
		absl::Cleanup unenforce{ [&]{ WebSessionGateTests::Enforce( schema, wasEnforced ); } };
		let permission = GrantSessionsRead( schema );
		absl::Cleanup revoke{ [&]{ Revoke( permission ); } };
		let before = Minted();
		absl::Cleanup removeBefore{ [before]{ Web::Server::Sessions::Remove( before ); } };
		let listing = serialize( jobject{{"query", "sessions{ endpoint }"}} );
		let authorization = Ƒ( "{:x}", before );
		EXPECT_FALSE( FailureStatus([&]{ Post(Port(), "/graphql", listing, authorization); }) ) << "the guest's grant";

		Logging::Capture warnings{ ELogLevel::Warning };
		RunQL( Ƒ("deleteUser( id:{} )", _user.Value) );
		absl::Cleanup restore{ []{ RunQL( Ƒ("restoreUser( id:{} )", _user.Value) ); } };
		EXPECT_FALSE( GuestPK() );
		EXPECT_TRUE( Warned(warnings, "was deleted") );
		EXPECT_EQ( UserOf(before).Value, _user.Value ) << "a session minted before keeps its user";
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/graphql", listing, authorization); }), http::status::forbidden ) << "a deleted user's";
		Guest::Resolve();//a start while it is deleted:  known, so the restore by id below is matched
		EXPECT_TRUE( Warned(warnings, "names a deleted user") );
		EXPECT_FALSE( GuestPK() );
		let after = Minted();
		absl::Cleanup removeAfter{ [after]{ Web::Server::Sessions::Remove( after ); } };
		EXPECT_FALSE( UserOf(after) );
		RunQL( Ƒ("restoreUser( id:{} )", _user.Value) );
		std::move( restore ).Cancel();
		EXPECT_EQ( GuestPK().Value, _user.Value ) << "restored by id";
	}

	//483-demo-guest-sessions review #2, #3:  every visitor's session has the guest's user, so none is a visitor's own.  With
	//app/sessions enforced, a guest reads another visitor's session - its address - or ends it only with the grant anyone would need.
	TEST_F( WebGuestTests, AGuestsSessionIsNobodysOwn ){
		let schema = AppSchema()->Name;
		let wasEnforced = WebSessionGateTests::Enforced( schema );
		WebSessionGateTests::Enforce( schema, true );
		absl::Cleanup restore{ [&]{ WebSessionGateTests::Enforce( schema, wasEnforced ); } };
		let visitor = Minted(), other = Minted();
		absl::Cleanup remove{ [=]{ Web::Server::Sessions::Remove( visitor ); Web::Server::Sessions::Remove( other ); } };
		ASSERT_EQ( UserOf(other).Value, UserOf(visitor).Value ) << "both the guest's";
		let authorization = Ƒ( "{:x}", visitor );
		let read = serialize( jobject{{"query", Ƒ("session( id:\"{:x}\" ){{ endpoint }}", other)}} );
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/graphql", read, authorization); }), http::status::forbidden ) << "another visitor's address";
		let purge = serialize( jobject{{"query", Ƒ("mutation purgeSession( id:\"{:x}\" )", other)}} );
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/graphql", purge, authorization); }), http::status::forbidden ) << "another visitor signed out";
		EXPECT_TRUE( Web::Server::Sessions::Find(other) );
	}

	//483-demo-guest-sessions review #7:  a deleted user renamed to the guest's slug is no guest - known, so its restore by id makes it one.
	TEST_F( WebGuestTests, ADeletedUserRenamedToTheSlugIsNoGuest ){
		RunQL( Ƒ("updateUser( id:{}, slug:\"{}-away\" )", _user.Value, Slug()) );//the slug free
		absl::Cleanup back{ []{ RunQL( Ƒ("updateUser( id:{}, slug:\"{}\" )", _user.Value, Slug()) ); } };
		EXPECT_FALSE( GuestPK() ) << "renamed away";
		let other = User( Slug()+"-deleted" );
		absl::Cleanup purge{ [other]{ RunQL( Ƒ("purgeUser( id:{} )", other.Value) ); } };
		RunQL( Ƒ("deleteUser( id:{} )", other.Value) );
		Logging::Capture warnings{ ELogLevel::Warning };
		RunQL( Ƒ("updateUser( id:{}, slug:\"{}\" )", other.Value, Slug()) );
		EXPECT_FALSE( GuestPK() ) << "a deleted user";
		EXPECT_TRUE( Warned(warnings, "A deleted user was renamed") );
		RunQL( Ƒ("restoreUser( id:{} )", other.Value) );
		EXPECT_EQ( GuestPK().Value, other.Value ) << "restored by id";
		RunQL( Ƒ("updateUser( id:{}, slug:\"{}-deleted\" )", other.Value, Slug()) );//away again, so the guest's own takes it back
		EXPECT_FALSE( GuestPK() );
	}

	//the seed creates the guest after the hub has started (plan Phase 8), and again after every reset:  the start's read finds no user
	//and says so, and the listener's userCreated sets it - and a purge, the reset's, clears it with a Warning.
	TEST_F( WebGuestTests, AnUnresolvedSlugWaitsForItsUser ){
		Logging::Capture warnings{ ELogLevel::Warning };
		Purge();
		EXPECT_FALSE( GuestPK() );
		EXPECT_TRUE( Warned(warnings, "was purged") );
		Guest::Resolve();//the start's read
		EXPECT_TRUE( Warned(warnings, "names no user") );
		let anonymous = Minted();
		absl::Cleanup removeAnonymous{ [anonymous]{ Web::Server::Sessions::Remove( anonymous ); } };
		EXPECT_FALSE( UserOf(anonymous) );
		_user = User( Slug() );//the seed's createUser
		EXPECT_EQ( GuestPK().Value, _user.Value );
		let guest = Minted();
		absl::Cleanup removeGuest{ [guest]{ Web::Server::Sessions::Remove( guest ); } };
		EXPECT_EQ( UserOf(guest).Value, _user.Value );
		Web::Server::Sessions::SetGuest( {} );
		Guest::Resolve();//the next start's read, the user there
		EXPECT_EQ( GuestPK().Value, _user.Value ) << "the start's read found no live user";
	}
}
#include <semaphore>
#include <thread>
#include <jde/db/meta/AppSchema.h>//GetSchema().Authorizer
#include "../src/UAConfig.h"
#include "../src/UATrust.h"
#include "../src/access/UAAccess.h"
#include "../src/access/OpcAuthorize.h"
#include "../src/ql/OpcQL.h"
#include <jde/fwk/log/MemoryLog.h>
#include <jde/ql/ql.h>
#define let const auto

namespace Jde::Opc::Server::Tests{
	using Access::ERights;
	constexpr ELogTags _tags{ ELogTags::Test };
	//access-review3 #4:  node ACLs are stored in the generic ERights vocabulary - what node-access.ts writes - and OpcAuthorize
	//translates them to UA access-level bits (ToAccess).  These seeds used to be UA bits, which the old C-cast passed through.
	struct AccessTests : ::testing::Test{
	protected:
		constexpr static ERights _readerAllowed = ERights::Read;
		constexpr static ERights _writerAllowed = ERights::Read | ERights::Update;
		constexpr static ERights _adminAllowed = _writerAllowed | ERights::Administer;
		constexpr static ERights _readerDenied = ERights::Update | ERights::Delete | ERights::Administer;
		constexpr static ERights _writerDenied = ERights::Administer;
		constexpr static ERights _adminDenied = ERights::None;
		//The criteria resource withholds everything, so a node it governs answers None whatever the role allows on the root.
		//It used to grant the *same* rights as the root, on `ns=4;i=6020` - a node no nodeset here loads - so UserAccess
		//passed identically whether _nodeResources worked or not (opcserver-review3 L30).  ServerStatus is in ns0, so the
		//browse actually reaches it, and it is not a node the other tests assert on.
		constexpr static ERights _nodeDenied = ERights::All;
		Ω criteriaNode()ι->UA_NodeId{ return UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVER_SERVERSTATUS ); }
		Ω roleSlug( const string& slug )ι->string{ return DB::Names::Capitalize( slug ); }

		Ω addRole( const string& slug, ERights allowed, ERights denied )ε->void{
			let userSlug = Ƒ( "{}User", slug );

			auto user = _app->QuerySync( "user(slug:$slug){id}", {{"slug", userSlug}} );
			if( user.empty() )
				user = _app->QuerySync<jobject>( "createUser( slug:$slug, name:$name ){id}", {{"slug", userSlug}, {"name", userSlug+" name"}} );
			let userId = Json::AsNumber<uint>( user.at("id") );
			_users.emplace( userSlug, userId );

			let slug_ = roleSlug( slug );
			//Find-or-create, as the user above already is:  createRole on a persisted db trips the roles.slug unique index
			//and takes the whole suite with it, which is what a non-ctest re-run (recreateDB=false, the documented way to run
			//beside live services) does every time (opcserver-review3 L30).
			auto role = _app->QuerySync( "role(slug:$slug){id}", {{"slug", slug_}} );
			let existed = !role.empty();
			if( !existed )
				role = _app->QuerySync<jobject>( "createRole( slug:$slug, name:$name ){id}", {{"slug",slug_}, {"name", slug_+" name"}} );
			let roleId = Json::AsNumber<Access::RolePK>( role.at("id") );
			_roles.emplace( slug_, roleId );//keyed as the db names it - the same key SetUpTestCase's roles() load uses, which is what made every guard below always-true.
			if( existed )
				return;//its permissions and acl are in the db already; re-adding them trips their unique indexes too.

			jobject vars{ {"roleId", roleId}, {"allowed", underlying(allowed)}, {"denied", underlying(denied)}, {"schema", _resource} };
			string query{ "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:$denied, resource:{schemaName:$schema, slug:\"nodeIds\"}} )" };
			_app->QuerySync<jvalue>( move(query), move(vars) );

			vars = { {"roleId", roleId}, {"allowed", underlying(allowed)}, {"denied", underlying(_nodeDenied)}, {"schema", _resource}, {"criteria", NodeId{criteriaNode()}.ToString()}, {"resourceName", "ServerStatus"} };
			query = "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:$denied, resource:{schemaName:$schema, slug:\"nodeIds\", criteria:$criteria, name:$resourceName}} )";
			_app->QuerySync<jvalue>( move(query), move(vars) );

			_app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", userId}, {"roleId", roleId}} );
		}
		Ω SetUpTestCase()ε->void{
			Server::Initialize( GetSchemaPtr() );
			_ua = &Server::GetUAServer();
			_app = AppClient();

			let nodeSlug = jobject{ {"slug","nodeIds"} };
			_app->QuerySync<jvalue>( "deleteResource( slug:$slug, criteria:null )", nodeSlug );
			let jroles = _app->QuerySync<jarray>( "roles(){ id slug }", {} );
			for( let& jrole : jroles )
				_roles.emplace( jrole.at("slug").get_string(), jrole.at("id").to_number<Access::RolePK>() );
			if( !_roles.contains(roleSlug("reader")) )//this program's own acl, once - re-creating it trips the same indexes.
				_app->QuerySync<jvalue>( "createAcl( identity:{id:$testProgUser}, permissionRight:{ allowed:$allowed, denied:0, resource:{schemaName: $schemaName, slug:$nodeResSlug}} )",
					{ {"testProgUser", AppClient()->UserPK().Value}, {"allowed", underlying(ERights::All)}, {"schemaName", _resource}, {"nodeResSlug", "nodeIds"} } );
			//Unconditional now that addRole is find-or-create:  it is also the only thing that fills _users, which the guards
			//used to skip on a re-run, leaving every _users.at() below to throw.
			addRole( "reader", _readerAllowed, _readerDenied );
			addRole( "writer", _writerAllowed, _writerDenied );
			addRole( "admin", _adminAllowed, _adminDenied );
			_app->QuerySync<jvalue>( "restoreResource( slug:$slug, criteria:null )", nodeSlug );
		}
		Ω TearDownTestCase()ι->void{}
		α SetUp()ι->void{}
//	private:
		static sp<App::Client::IAppClient> _app;
		static inline flat_map<string, uint> _roles;
		static inline flat_map<string, uint> _users;
		static string _resource;
		static UAServer* _ua;
	};
	UAServer* AccessTests::_ua{ nullptr };
	sp<App::Client::IAppClient> AccessTests::_app{ AppClient() };
	string AccessTests::_resource{ "opc."+Settings::FindString("/opcServer/resource").value_or("test") };

	TEST_F( AccessTests, UserAccess ){
		let nodeId = UA_NODEID_NUMERIC( 4, 6020 );
		let accessLevel = [&]( const string& user ){
			UAAccess::SessionContext ctx{ "", TimePoint::max(), 0, {(UserPK::Type)_users.at(user)} };
			return (EAccess)UAAccess::GetUserAccessLevel( _ua->Ptr(), nullptr, nullptr, &ctx, &nodeId, nullptr );
		};
		EXPECT_EQ( accessLevel("readerUser"), ToAccess(_readerAllowed) ); //Read|HistoryRead...
		EXPECT_EQ( underlying(accessLevel("readerUser")) & UA_ACCESSLEVELMASK_WRITE, 0u ); //...and not WRITE, which the cast handed a reader (ERights::Read is 0x2).
		EXPECT_EQ( accessLevel("writerUser"), ToAccess(_writerAllowed) ); //+Write|HistoryWrite; cast raw, Read|Update=0x6 was WRITE|HISTORYREAD.
		EXPECT_EQ( accessLevel("adminUser"), ToAccess(_adminAllowed) ); //EAccess::All

		//The criteria resource, on a node the browse actually reaches: it withholds everything, so these answer None while
		//the root above answers the role's rights.  Identical rights on a node no nodeset loaded made this pass either way.
		let statusNode = criteriaNode();
		let statusAccess = [&]( const string& user ){
			UAAccess::SessionContext ctx{ "", TimePoint::max(), 0, {(UserPK::Type)_users.at(user)} };
			return (EAccess)UAAccess::GetUserAccessLevel( _ua->Ptr(), nullptr, nullptr, &ctx, &statusNode, nullptr );
		};
		EXPECT_EQ( statusAccess("readerUser"), EAccess::None ) << "governed by its own resource, not the root";
		EXPECT_EQ( statusAccess("adminUser"), EAccess::None );
	}

	//opcserver-review3 #8:  getUserRightsMask, allowBrowseNode and allowAddReference used to ask Authorize for the resource
	//names "node", "browse" and "variables" - names nothing in the repo ever creates, and Authorize answers a missing name
	//with ERights::All / a silent Test.  So every session, including one holding a *denied* nodeIds right, got every
	//UA_WRITEMASK bit the node's own writeMask allowed (DisplayName, Description and AccessLevel - and AccessLevel is what
	//gates the Value write), browsed the whole tree, and could add references.  All three now read the node's own acl.
	TEST_F( AccessTests, NodeRightsGateWriteMaskBrowseAndAddReference ){
		let nodeId = UA_NODEID_NUMERIC( 4, 6020 );
		let context = []( uint userPK ){ return UAAccess::SessionContext{ "", TimePoint::max(), 0, {(UserPK::Type)userPK} }; };
		let writeMask = [&]( uint userPK ){
			auto ctx = context( userPK );
			return UAAccess::GetUserRightsMask( _ua->Ptr(), nullptr, nullptr, &ctx, &nodeId, nullptr );
		};
		let mayBrowse = [&]( uint userPK ){
			auto ctx = context( userPK );
			return UAAccess::AllowBrowseNode( _ua->Ptr(), nullptr, nullptr, &ctx, &nodeId, nullptr );
		};
		let mayAddReference = [&]( uint userPK ){
			auto ctx = context( userPK );
			UA_AddReferencesItem item; UA_AddReferencesItem_init( &item );
			item.sourceNodeId = nodeId;
			return UAAccess::AllowAddReference( _ua->Ptr(), nullptr, nullptr, &ctx, &item );
		};
		let unknownUser = uint{ std::numeric_limits<UserPK::Type>::max() };//no acl row at all - the shape a fail-open answers All for.

		EXPECT_EQ( writeMask(_users.at("readerUser")), 0u ) << "a reader holds neither Update nor Administer, so no attribute is writable";
		EXPECT_EQ( writeMask(unknownUser), 0u );
		EXPECT_NE( writeMask(_users.at("writerUser")) & UA_WRITEMASK_DISPLAYNAME, 0u );//Update
		EXPECT_EQ( writeMask(_users.at("writerUser")) & UA_WRITEMASK_ACCESSLEVEL, 0u ) << "AccessLevel is the Value-write gate - Administer only";
		EXPECT_NE( writeMask(_users.at("adminUser")) & UA_WRITEMASK_ACCESSLEVEL, 0u );

		EXPECT_TRUE( mayBrowse(_users.at("readerUser")) );//Read
		EXPECT_FALSE( mayBrowse(unknownUser) );

		EXPECT_FALSE( mayAddReference(_users.at("readerUser")) ) << "a reference is a write to its source node";
		EXPECT_TRUE( mayAddReference(_users.at("writerUser")) );//Update
		EXPECT_FALSE( mayAddReference(unknownUser) );
	}

	//soak-findings #9:  a session that times out keeps its subscriptions for TransferSubscriptions, and open62541 goes on sampling
	//their monitored items with no session at all - the read callbacks then arrive with a null session id and a null context,
	//which open62541 documents as normal and expects to be denied.  They were denied, but only after `ASSERT( ctx )` logged a
	//CRITICAL: twice a second (the sampling interval) for the subscription's remaining lifetime, after every lost session - 1,432
	//of them in the 12 minutes the 09-14 soak ran on after its session timed out.  The sessionless read must be denied quietly;
	//a real session that arrives without its context is still a bug and must still say so.
	TEST_F( AccessTests, ASessionlessReadIsDeniedWithoutAnAssert ){
		let nodeId = UA_NODEID_NUMERIC( 4, 6020 );
		//Message(), not Text:  Text is the format string "Assert:  {} is false", which never contains the argument.
		let asserts = []{ return Logging::Find( [](const Logging::Entry& e){ return e.Level==ELogLevel::Critical && e.Message().contains("ctx is false"); } ).size(); };

		Logging::ClearMemory();
		EXPECT_EQ( UAAccess::GetUserAccessLevel(_ua->Ptr(), nullptr, nullptr, nullptr, &nodeId, nullptr), 0 ) << "a detached subscription's sample reads nothing";
		EXPECT_EQ( UAAccess::GetUserRightsMask(_ua->Ptr(), nullptr, nullptr, nullptr, &nodeId, nullptr), 0u );
		EXPECT_EQ( asserts(), 0u ) << "the sessionless read is open62541's design, not a fault - it must not log a CRITICAL";

		const UA_NodeId sessionId = UA_NODEID_NUMERIC( 1, 42 );
		Logging::ClearMemory();
		EXPECT_EQ( UAAccess::GetUserAccessLevel(_ua->Ptr(), nullptr, &sessionId, nullptr, &nodeId, nullptr), 0 ) << "still denied";
		EXPECT_EQ( UAAccess::GetUserRightsMask(_ua->Ptr(), nullptr, &sessionId, nullptr, &nodeId, nullptr), 0u );
		EXPECT_EQ( asserts(), 2u ) << "a session without the context ActivateSession installs is a real bug and must still assert";
	}

	//appserver-review3 #13:  the AppServer's delegated admin check, answered here (OpcServerQL's adminCheck →
	//OpcAuthorize::TestAdminNode):  who may grant on a node is whoever administers the resource governing it - its own row when
	//it has one, else the nearest configured ancestor's, else root.  The criteria row is created by SetUpTestCase after
	//AssignRights ran, so on a fresh db it is unmapped and inherits the root;  on a persisted one it is its own resource - the
	//roles grant the same rights on both, so the answers hold either way.
	TEST_F( AccessTests, DelegatedAdminAnswer ){
		auto isAdmin = [&]( const string& user, const string& criteria )->bool{
			jobject vars{ {"user", _users.at(user)}, {"resource", "nodeIds"}, {"criteria", criteria} };
			auto q = QL::Parse( "adminCheck( user:$user ){isAdmin resource( resource:$resource, criteria:$criteria )}", move(vars), {} );//as AppClientSocketSession::ClientQuery parses it: no schema.
			let y = _app->ClientQuery( move(q), _app->UserPK() )->await_resume();
			return Json::AsObject( y ).at( "adminCheck" ).at( "isAdmin" ).as_bool();
		};
		EXPECT_TRUE( isAdmin("adminUser", "") ) << "Administer on the root";
		EXPECT_FALSE( isAdmin("readerUser", "") );
		EXPECT_FALSE( isAdmin("writerUser", "") ) << "Administer denied explicitly";
		EXPECT_TRUE( isAdmin("adminUser", "ns=4;i=6020") );
		EXPECT_FALSE( isAdmin("readerUser", "ns=4;i=6020") );
		EXPECT_TRUE( isAdmin("adminUser", "ns=4;i=6021") ) << "unmapped - inherits what protects it";
		EXPECT_FALSE( isAdmin("readerUser", "ns=4;i=6021") );
	}

	//the mapping on its own, without a server.
	TEST( ToAccessTests, GenericRightsTranslateToAccessLevelBits ){
		static_assert( ToAccess(ERights::All)==EAccess::All );
		static_assert( ToAccess(ERights::None)==EAccess::None );
		EXPECT_EQ( ToAccess(ERights::Read), EAccess::Read | EAccess::HistoryRead );
		EXPECT_EQ( underlying(ToAccess(ERights::Read)) & UA_ACCESSLEVELMASK_WRITE, 0u );
		EXPECT_NE( underlying(ToAccess(ERights::Update)) & UA_ACCESSLEVELMASK_WRITE, 0u );
		EXPECT_EQ( underlying(ToAccess(ERights::Update)) & UA_ACCESSLEVELMASK_HISTORYREAD, 0u );
		EXPECT_EQ( underlying(ToAccess(ERights::Create)) & UA_ACCESSLEVELMASK_READ, 0u );
		EXPECT_EQ( ToAccess(ERights::Create | ERights::Purge | ERights::Subscribe | ERights::Execute), EAccess::None );
		EXPECT_EQ( ToAccess(ERights::Delete), EAccess::HistoryWrite );
		EXPECT_EQ( ToAccess(ERights::Administer), EAccess::StatusWrite | EAccess::TimestampWrite | EAccess::SemanticChange );
	}
	//opcserver-review3 #9:  CustomMutation routed every updateLogSetting* straight to the client await with no credential
	//check, while LogSettingsQuery beside it required a user - so an anonymous POST to /graphql on the web listener rewrote
	//the live SpdLog/ProtoLog levels and, `persist` defaulting on, had the AppServer store them in instance_tag_levels under
	//the OpcServer's own identity.  Nothing here resumes an await, so no level moves and no round trip is made.
	struct CustomMutationTests : ::testing::Test{
	protected:
		Ω mutation( string commandName )ε->QL::MutationQL{//system: a registered system table resolves against no schema, as QL::Parse admits it.
			return QL::MutationQL{ move(commandName), jobject{}, ms<jobject>(), optional<QL::TableQL>{}, true, Server::Schemas(), true };
		}
		//The status a refusal carries, or empty when the caller got a real await instead (only a refusal is ready).
		Ω refusalStatus( QL::MutationQL& m, UserPK executer )ε->optional<EHttpStatus>{
			auto y = Server::QL().CustomMutation( m, QL::Creds{executer}, SRCE_CUR );
			if( !y || !y->await_ready() )
				return {};
			try{
				y->await_resume();
			}
			catch( const Exception& e ){
				return e.HttpStatus();
			}
			return {};
		}
	};

	TEST_F( CustomMutationTests, AnonymousLogSettingsIsRefused ){
		auto m = mutation( "updateLogSetting" );
		EXPECT_EQ( refusalStatus(m, UserPK{}), EHttpStatus::Unauthorized );//401, not 403: it is about who is asking.
	}
	TEST_F( CustomMutationTests, AuthenticatedLogSettingsStillRoutes ){
		auto m = mutation( "updateLogSetting" );//the AppServer's push arrives this way, carrying the admin who made the change.
		auto y = Server::QL().CustomMutation( m, QL::Creds{UserPK{7}}, SRCE_CUR );
		ASSERT_TRUE( y );
		EXPECT_FALSE( y->await_ready() ) << "LogSettingsClientMAwait suspends; only a refusal answers ready";
	}
	TEST_F( CustomMutationTests, EveryOtherMutationStaysForbidden ){
		auto m = mutation( "createObject" );
		EXPECT_EQ( refusalStatus(m, UserPK{}), EHttpStatus::Forbidden );//#3, unchanged by #9's split.
		EXPECT_EQ( refusalStatus(m, UserPK{7}), EHttpStatus::Forbidden ) << "authenticated is not the gate here - the schema owns no tables";
		EXPECT_FALSE( Server::QL().CustomMutation(m, QL::Creds{UserPK{UserPK::System}}, SRCE_CUR) ) << "the schema sync's .mutation files still write";
	}

	TEST_F( AccessTests, Query ){
		auto q = "roles{ id name permissionRight{id allowed denied resource(schemaName:$schemaName, slug:$slug, criteria:$criteria){id criteria}} }";
		jobject vars{ {"schemaName", "opc.default"}, {"slug", "nodeIds"}, {"criteria", jarray{jvalue{}}} };
		TRACE( "{}", q );
		TRACE( "{}", serialize(vars) );
		let result = _app->QuerySync<jvalue>( move(q), move(vars) );
		TRACE( "{}", serialize(result) );
	}

	//opcserver-review3 #10:  AssignRights held _nodeResourcesMutex exclusively across UA_Server_browse, which takes the
	//server's serviceMutex - while open62541 calls the access-control plugin *with* serviceMutex already held, so a client
	//Read on the UA thread landed in NodeRights wanting the shared lock.  Neither side could proceed.
	//A node-lifecycle constructor runs from exactly that position (recursiveCallConstructors asserts the serviceMutex), so
	//it stands in for the UA thread here without needing a client:  it signals, holds the lock while the other thread runs
	//AssignRights, then asks NodeRights.  On the pre-fix code both threads park forever;  now AssignRights browses with
	//nothing held and simply waits its turn for serviceMutex.  A deadlock can only be observed by not finishing, so the
	//failure here is the suite's ctest TIMEOUT rather than an EXPECT - verified by restoring the old lock scope, where the
	//binary sits in this test until it is killed (exit 124) instead of the ~260ms it takes now.  Last in the fixture - it
	//re-runs AssignRights, so it must not precede the tests that read the map it rebuilds.
	TEST_F( AccessTests, AssignRightsHoldsNoLockAcrossTheBrowse ){
		auto server = _ua->Ptr();
		ASSERT_TRUE( server );
		auto& auth = static_cast<OpcAuthorize&>( *GetSchema().Authorizer );
		static std::binary_semaphore holdingServiceMutex{ 0 };
		static std::atomic<bool> askedNodeRights{ false };
		static OpcAuthorize* authorizer{ &auth };//the constructor is a C callback: no captures, so the state is static.

		UA_NodeTypeLifecycle lifecycle{};
		lifecycle.constructor = []( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId*, void** )->UA_StatusCode{
			holdingServiceMutex.release();
			std::this_thread::sleep_for( 250ms );//long enough that AssignRights is inside its browse, wanting this lock.
			authorizer->NodeRights( NodeId::ObjectsFolder(), UserPK{7} );//pre-fix: blocks on the exclusive _nodeResourcesMutex AssignRights is holding.
			askedNodeRights = true;
			return UA_STATUSCODE_GOOD;
		};
		let baseObjectType = UA_NODEID_NUMERIC( 0, UA_NS0ID_BASEOBJECTTYPE );
		ASSERT_EQ( UA_Server_setNodeTypeLifecycle(server, baseObjectType, lifecycle), UA_STATUSCODE_GOOD );

		{
			std::jthread adder{ [server, baseObjectType]{
				UA_NodeId added;
				UA_Server_addObjectNode( server, UA_NODEID_NULL, UA_NODEID_NUMERIC(0,UA_NS0ID_OBJECTSFOLDER),
					UA_NODEID_NUMERIC(0,UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(1,(char*)"opcserver-review3-10"),
					baseObjectType, UA_ObjectAttributes_default, nullptr, &added );
			} };
			ASSERT_TRUE( holdingServiceMutex.try_acquire_for(10s) ) << "the type constructor never ran - the test would be vacuous";
			auth.AssignRights( *server );
		}
		UA_Server_setNodeTypeLifecycle( server, baseObjectType, UA_NodeTypeLifecycle{} );//leave the server as it was found.
		EXPECT_TRUE( askedNodeRights );
		//Non-vacuity: AssignRights returns before the browse when no base resource is configured, and this fixture's are.
		EXPECT_EQ( auth.NodeRights(NodeId::ObjectsFolder(), UserPK{(UserPK::Type)_users.at("readerUser")}), _readerAllowed );
	}

	//opcserver-review3 L27:  LoadTrustList is ι but caught only OpenSslException and filesystem_error, while
	//Crypto::ReadCertificate reaches Crypto::Internal::File, which throws IO::IOException for a path that is not there -
	//unrelated to either, so it crossed the noexcept boundary and terminated the process.
	//This does NOT reproduce that:  the only way to reach that read with the file gone is the TOCTOU between the directory
	//iterator and it, and a dangling symlink - the obvious stand-in - throws filesystem_error out of last_write_time first,
	//which was always caught.  What it pins is the contract the finding is about:  a bad entry does not escape the ι scan,
	//and does not cost the directory's other certificates their trust.
	TEST( TrustListTests, AnUnreadableEntryDoesNotEscapeTheNoexceptScan ){
		let dirs = Settings::FindStringArray( "/access/trustedCertDirs" );
		ASSERT_FALSE( dirs.empty() );
		const fs::path dir{ dirs.front() };
		std::error_code ec; fs::create_directories( dir, ec );
		let dangling = dir/"opcserver-review3-L27.pem";
		fs::remove( dangling, ec );
		fs::create_symlink( dir/"no-such-certificate-file", dangling, ec );
		ASSERT_FALSE( ec ) << "could not stage the dangling symlink: " << ec.message();

		UA_TrustListDataType list; UA_TrustListDataType_init( &list );
		EXPECT_NO_THROW( UATrust::LoadTrustList(list) );//it is ι - anything escaping is std::terminate, not a throw the test sees.
		UA_TrustListDataType_clear( &list );

		fs::remove( dangling, ec );
		UA_TrustListDataType after; UA_TrustListDataType_init( &after );
		UATrust::LoadTrustList( after );//resync the mtime cache so the rest of the process sees the real trust list.
		EXPECT_NE( after.trustedCertificatesSize, 0u ) << "the good certificates in that directory are still trusted";
		UA_TrustListDataType_clear( &after );
	}

	//install-issues #33, the server's half:  a UA client publishes its instance certificate as DER, and this scan took
	//`.pem`/`.crt` only - so a client certificate copied into /access/trustedCertDirs in the form its owner actually writes
	//was passed over in silence, and the handshake was refused with nothing in the log to say why.  Staged in the real
	//directory and removed again, as the test above does, with the cache resynced so the running server's trust is as found.
	TEST( TrustListTests, ADerEntryIsTrusted ){
		let dirs = Settings::FindStringArray( "/access/trustedCertDirs" );
		ASSERT_FALSE( dirs.empty() );
		const fs::path dir{ dirs.front() };
		std::error_code ec; fs::create_directories( dir, ec );

		UA_TrustListDataType before; UA_TrustListDataType_init( &before );
		UATrust::LoadTrustList( before );
		let baseline = before.trustedCertificatesSize;
		UA_TrustListDataType_clear( &before );
		ASSERT_NE( baseline, 0u ) << "the suite's own client certificate should already be anchored here";

		//a certificate this directory does not hold, written in the encoding a third-party server publishes.  Issued into a
		//sibling scratch dir, never into the trust dir itself - a .pem there would be anchored and the test would pass blind.
		let scratch = dir.parent_path()/"install-issues-33";
		fs::remove_all( scratch, ec ); fs::create_directories( scratch, ec );
		let settings = Crypto::CryptoSettings{ jobject{
			{"certificate", jobject{{"path", (scratch/"cert.pem").string()}, {"commonName", "install-issues-33"}, {"company", "jde-cpp"}, {"country", "US"}}},
			{"privateKey", jobject{{"path", (scratch/"private.pem").string()}}},
			{"publicKey", jobject{{"path", (scratch/"public.pem").string()}}},
			{"dh", ""}
		}, {} };
		Crypto::CreateKeyCertificate( settings );
		let staged = dir/"install-issues-33.der";
		fs::remove( staged, ec );
		let der = Crypto::ReadCertificate( settings.Certificate.Path );
		IO::SaveBinary<const byte>( staged, std::span{der} );

		UA_TrustListDataType list; UA_TrustListDataType_init( &list );
		UATrust::LoadTrustList( list );
		EXPECT_EQ( list.trustedCertificatesSize, baseline+1 ) << "the .der was passed over";
		UA_TrustListDataType_clear( &list );

		fs::remove( staged, ec );
		fs::remove_all( scratch, ec );
		UA_TrustListDataType after; UA_TrustListDataType_init( &after );
		UATrust::LoadTrustList( after );//resync the mtime cache - the rest of the process must see the real trust list.
		EXPECT_EQ( after.trustedCertificatesSize, baseline );
		UA_TrustListDataType_clear( &after );
	}

	//security-matrix #5:  there is no unsecured shape.  A config with no /opcServer/ssl - a hidden `ssl::`, a mistyped key - used to
	//build a None-only server with no certificate and no trust list;  now the server refuses to start, and names the setting.
	TEST( UAConfigTests, NoSslIsRefused ){
		let ssl = Settings::FindObject( "/opcServer/ssl" );
		ASSERT_TRUE( ssl ) << "the suite's own server runs secured";
		const jobject original{ *ssl };
		struct Restore final{ const jobject& Ssl; ~Restore(){ try{ Settings::Set("/opcServer/ssl", Ssl); }catch( const std::exception& ){} } } restore{ original };
		Settings::Set( "/opcServer/ssl", jvalue{} );//null - FindObject answers nullptr for it, as for an absent key.
		ASSERT_FALSE( Settings::FindObject("/opcServer/ssl") );
		try{
			UAConfig config;
			FAIL() << "a server config was built without /opcServer/ssl";
		}
		catch( const std::exception& e ){
			EXPECT_TRUE( string{e.what()}.contains("/opcServer/ssl") ) << e.what();
		}
	}

	//install-issues "Noise in a production log":  a trusted dir that is not there - a product not installed, or not started
	//yet - is one Warning, not one per scan; a failed verify rescans, so it used to repeat for the life of the process.
	//Later sightings are Debug.  The dir is appended to the real list and the list restored, so the running server's trust
	//is untouched.
	TEST( TrustListTests, AMissingDirectoryWarnsOnce ){
		let dirs = Settings::FindStringArray( "/access/trustedCertDirs" );
		ASSERT_FALSE( dirs.empty() );
		let missing = fs::path{ dirs.front() }.parent_path()/Ƒ( "missing-{}", Process::ProcessId() );//never created - and a name no earlier scan has seen.
		jarray withMissing; for( let& d : dirs ) withMissing.push_back( jvalue{d} );
		withMissing.push_back( jvalue{missing.string()} );
		Settings::Set( "/access/trustedCertDirs", withMissing );
		Logging::ClearMemory();
		for( uint i=0; i<3; ++i ){
			UA_TrustListDataType list; UA_TrustListDataType_init( &list );
			UATrust::LoadTrustList( list );
			UA_TrustListDataType_clear( &list );
		}
		jarray original; for( let& d : dirs ) original.push_back( jvalue{d} );
		Settings::Set( "/access/trustedCertDirs", original );

		let lines = Logging::Find( [&](const Logging::Entry& e){ return e.Text.contains("does not exist") && e.Message().contains(missing.string()); } );
		ASSERT_EQ( lines.size(), 3u ) << "one line per scan";
		EXPECT_EQ( lines[0].Level, ELogLevel::Warning );
		EXPECT_EQ( lines[1].Level, ELogLevel::Debug );
		EXPECT_EQ( lines[2].Level, ELogLevel::Debug );
	}

	//opcserver-review3 L24:  every accepting branch of ActivateSession assigned straight through *sessionContext, and
	//open62541 passes &session->context on *every* activation - re-activation is explicitly allowed, and the vendor's own
	//client re-activates on a channel renew - while closeSession only ever sees the last pointer.  So each re-activation
	//dropped the previous context on the floor.  Nothing but the gateway suite drives activation at all, and nothing
	//drives it twice on one session;  this does.
	TEST_F( AccessTests, ReactivatingASessionReplacesItsContext ){
		let jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		let token = jwt.Payload();//the wire form the gateway sends (TokenTests does the same).
		UA_IssuedIdentityToken issued; UA_IssuedIdentityToken_init( &issued );
		issued.tokenData = UA_BYTESTRING_ALLOC( token.c_str() );
		UA_ExtensionObject identity; UA_ExtensionObject_init( &identity );
		UA_ExtensionObject_setValueNoDelete( &identity, &issued, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );

		void* slot{};
		auto& accessControl = UA_Server_getConfig( _ua->Ptr() )->accessControl;//ActivateSession reads ac->context before it branches.
		let activate = [&]{ return UAAccess::ActivateSession( _ua->Ptr(), &accessControl, nullptr, nullptr, nullptr, &identity, &slot ); };
		ASSERT_EQ( activate(), UA_STATUSCODE_GOOD );
		let first = slot; ASSERT_TRUE( first );
		EXPECT_EQ( static_cast<UAAccess::SessionContext*>(slot)->UserPK, AppClient()->UserPK() );

		ASSERT_EQ( activate(), UA_STATUSCODE_GOOD ) << "re-activation is allowed and must not be refused";
		EXPECT_TRUE( slot );
		EXPECT_EQ( static_cast<UAAccess::SessionContext*>(slot)->UserPK, AppClient()->UserPK() ) << "the replacement carries the same identity";
		//`first` is freed by the second activation - the fix.  Not asserted: the free itself is not observable here, since
		//Process::Shutdown ends in std::_Exit and LSan never runs, and a recoverable leak check would trip on the suite's
		//pre-existing ones (opcserver-review #23).
		UAAccess::CloseSession( _ua->Ptr(), nullptr, nullptr, slot );
		UA_IssuedIdentityToken_clear( &issued );
	}

	//opcserver-review3 L20:  the session-expiry renewal was a BlockAwait made from inside the access-control callbacks -
	//which open62541 calls with its serviceMutex held - so a hung AppServer froze every OPC client for the socket deadline,
	//and the timeout then tore down the app-client socket.  It is now posted and answered on the io thread; the UA thread
	//serves the lapsed snapshot while the question is outstanding, bounded by one renewal interval, and any answer -
	//including a failure - ends that grace.  These drive GetUserAccessLevel, which is what consults it.
	TEST_F( AccessTests, ALapsedSessionIsServedWhileTheRenewalIsOutstandingThenDenied ){
		let nodeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVER );
		let readerPK = (UserPK::Type)_users.at( "readerUser" );
		let access = [&]( UAAccess::SessionContext& ctx ){ return (EAccess)UAAccess::GetUserAccessLevel( _ua->Ptr(), nullptr, nullptr, &ctx, &nodeId, nullptr ); };

		//No session id: nothing to ask the authority about, so a lapsed snapshot is simply expired.
		UAAccess::SessionContext noId{ "", Clock::now()-1s, 0, {readerPK} };
		EXPECT_EQ( access(noId), EAccess::None );

		//Lapsed well past the renewal interval: outstanding or not, the snapshot is too old to serve.
		UAAccess::SessionContext stale{ "", Clock::now()-10min, 4242, {readerPK} };
		EXPECT_EQ( access(stale), EAccess::None );

		//Just lapsed, renewal outstanding: served rather than denied - and it returned, which is the whole point.
		UAAccess::SessionContext lapsed{ "", Clock::now()-1s, 4243, {readerPK} };
		let start = Clock::now();
		EXPECT_EQ( access(lapsed), ToAccess(_readerAllowed) );
		EXPECT_LT( Clock::now()-start, 5s ) << "the callback must not wait on the AppServer";

		//4243 is not a session the AppServer knows, so the renewal fails - and a failure is an answer, which ends the grace.
		for( uint i=0; i<100 && access(lapsed)!=EAccess::None; ++i )
			std::this_thread::sleep_for( 50ms );
		EXPECT_EQ( access(lapsed), EAccess::None ) << "once the authority has answered, the lapsed snapshot is no longer served";
	}

	//access-review3 L22:  _nodeResources was built once, by the AssignRights that startup runs, and nothing rebuilt it - so
	//a criteria-scoped `nodeIds` resource created afterwards (what the node-access page writes) was never mapped.  The node
	//kept falling back to the root's rights, and a per-node allow or deny did nothing until the process restarted.
	//OpcAuthorize now overrides the two Authorize hooks the AccessListener already calls and re-maps on them;  this drives
	//them exactly as AccessListener::ResourceChanged does, so no QL round trip is needed.
	TEST_F( AccessTests, ACriteriaResourceCreatedAfterStartupIsMapped ){
		auto& auth = static_cast<OpcAuthorize&>( *GetSchema().Authorizer );
		let reader = UserPK{ (UserPK::Type)_users.at("readerUser") };
		const NodeId serverNode{ UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER) };//ns0, and ObjectsFolder organizes it - the browse reaches it.
		constexpr Access::ResourcePK scratchPK{ 999999 };//no row owns this; only the in-memory maps are touched.

		ASSERT_EQ( auth.NodeRights(serverNode, reader), _readerAllowed ) << "before: the node inherits the root resource";

		auth.CreateResource( Access::Resource{scratchPK, jobject{ {"schemaName",_resource}, {"slug","nodeIds"}, {"criteria",serverNode.ToString()} }} );
		EXPECT_EQ( auth.NodeRights(serverNode, reader), Access::ERights::None ) << "after: its own resource governs it, and the reader holds nothing on that one";
		EXPECT_EQ( auth.NodeRights(NodeId::ObjectsFolder(), reader), _readerAllowed ) << "the root is untouched";

		auth.UpdateResourceDeleted( scratchPK, _resource, jobject{{"id",scratchPK}}, false );
		EXPECT_EQ( auth.NodeRights(serverNode, reader), _readerAllowed ) << "deleting it hands the node back to the root, also without a restart";
	}
}

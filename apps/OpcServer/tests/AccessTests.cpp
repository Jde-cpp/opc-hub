#include <semaphore>
#include <absl/cleanup/cleanup.h>
#include <thread>
#include <jde/db/meta/AppSchema.h>//GetSchema().Authorizer
#include <jde/opc/server/UAConfig.h>
#include <jde/opc/server/UATrust.h>
#include <jde/opc/server/UAAccess.h>
#include "../src/access/OpcAuthorize.h"
#include "../src/ql/OpcQL.h"
#include "helpers.h"
#include <jde/fwk/log/MemoryLog.h>
#include <jde/ql/ql.h>
#include <jde/ql/QLAwait.h>
#include <jde/web/server/Sessions.h>
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
		//The criteria resource withholds everything, so a node it governs answers None whatever the role allows on the root:  a
		//node's grants add to the root's, and a deny on either wins.
		//It used to grant the *same* rights as the root, on `ns=4;i=6020` - a node no nodeset here loads - so UserAccess
		//passed identically whether _nodeResources worked or not (opcserver-review3 L30).  ServerStatus is in ns0, so the
		//browse actually reaches it, and it is not a node the other tests assert on.
		constexpr static ERights _nodeDenied = ERights::All;
		Ω criteriaNode()ι->UA_NodeId{ return UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVER_SERVERSTATUS ); }
		Ω roleSlug( const string& slug )ι->string{ return DB::Names::Capitalize( slug ); }

		//A `branch` role holds `allowed`/`denied` on ServerStatus alone and nothing on the root - the branch-restricted user
		//the folder-listing browse rule is for.  The others hold them on the root, and ServerStatus withholds everything.
		Ω addRole( const string& slug, ERights allowed, ERights denied, bool branch=false )ε->void{
			let userSlug = Ƒ( "{}User", slug );
			let user = FindOrCreateUser( *_app, userSlug );
			_users.emplace( userSlug, user.Value );
			let criteria = NodeId{ criteriaNode() }.ToString();
			vector<RoleGrant> grants;
			if( !branch )
				grants.push_back( {_resource, "nodeIds", allowed, denied} );
			grants.push_back( {_resource, "nodeIds", allowed, branch ? denied : _nodeDenied, criteria, "ServerStatus"} );
			let role = roleSlug( slug );
			_roles.emplace( role, GrantRole(*_app, role, user, grants).Value );//keyed as the db names it - the same key SetUpTestCase's roles() load uses.
		}
		Ω SetUpTestCase()ε->void{
			_resource = InstanceSchema();//here, not at its definition:  static initialization runs before the settings load.
			Server::Initialize( GetSchemaPtr() );
			_ua = &Server::GetUAServer();
			_app = AppClient();

			let nodeSlug = jobject{ {"slug","nodeIds"} };
			_app->QuerySync<jvalue>( "deleteResource( slug:$slug, criteria:null )", nodeSlug );
			let jroles = _app->QuerySync<jarray>( "roles( deleted:null ){ id slug }", {} );
			for( let& jrole : jroles )
				_roles.emplace( jrole.at("slug").get_string(), jrole.at("id").to_number<Access::RolePK::Type>() );
			if( !_roles.contains(roleSlug("reader")) )//this program's own acl, once - re-creating it trips the same indexes.
				_app->QuerySync<jvalue>( "createAcl( identity:{id:$testProgUser}, permissionRight:{ allowed:$allowed, denied:0, resource:{schemaName: $schemaName, slug:$nodeResSlug}} )",
					{ {"testProgUser", AppClient()->UserPK().Value}, {"allowed", underlying(ERights::All)}, {"schemaName", _resource}, {"nodeResSlug", "nodeIds"} } );
			//Unconditional now that addRole is find-or-create:  it is also the only thing that fills _users, which the guards
			//used to skip on a re-run, leaving every _users.at() below to throw.
			addRole( "reader", _readerAllowed, _readerDenied );
			addRole( "writer", _writerAllowed, _writerDenied );
			addRole( "admin", _adminAllowed, _adminDenied );
			addRole( "branch", ERights::Read, ERights::None, true );
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
	string AccessTests::_resource;

	TEST_F( AccessTests, UserAccess ){
		let nodeId = UA_NODEID_NUMERIC( 4, 6020 );
		let accessLevel = [&]( const string& user ){ return AccessLevel( nodeId, UserPK{(UserPK::Type)_users.at(user)} ); };
		EXPECT_EQ( accessLevel("readerUser"), ToAccess(_readerAllowed) ); //Read|HistoryRead...
		EXPECT_EQ( underlying(accessLevel("readerUser")) & UA_ACCESSLEVELMASK_WRITE, 0u ); //...and not WRITE, which the cast handed a reader (ERights::Read is 0x2).
		EXPECT_EQ( accessLevel("writerUser"), ToAccess(_writerAllowed) ); //+Write|HistoryWrite; cast raw, Read|Update=0x6 was WRITE|HISTORYREAD.
		EXPECT_EQ( accessLevel("adminUser"), ToAccess(_adminAllowed) ); //EAccess::All

		//The criteria resource, on a node the browse actually reaches: it withholds everything, so these answer None while
		//the root above answers the role's rights.  Identical rights on a node no nodeset loaded made this pass either way.
		let statusNode = criteriaNode();
		let statusAccess = [&]( const string& user ){ return AccessLevel( statusNode, UserPK{(UserPK::Type)_users.at(user)} ); };
		EXPECT_EQ( statusAccess("readerUser"), EAccess::None ) << "its own resource's deny wins over the root's grant";
		EXPECT_EQ( statusAccess("adminUser"), EAccess::None );
	}

	//opcserver-review3 #8:  getUserRightsMask, allowBrowseNode and allowAddReference used to ask Authorize for the resource
	//names "node", "browse" and "variables" - names nothing in the repo ever creates, and Authorize answers a missing name
	//with ERights::All / a silent Test.  So every session, including one holding a *denied* nodeIds right, got every
	//UA_WRITEMASK bit the node's own writeMask allowed (DisplayName, Description and AccessLevel - and AccessLevel is what
	//gates the Value write), browsed the whole tree, and could add references.  All three now read the node's own acl.
	TEST_F( AccessTests, NodeRightsGateWriteMaskBrowseAndAddReference ){
		let nodeId = UA_NODEID_NUMERIC( 4, 6020 );
		let context = []( uint userPK ){ return Session( UserPK{(UserPK::Type)userPK} ); };
		let writeMask = [&]( uint userPK ){
			auto ctx = context( userPK );
			return UAAccess::GetUserRightsMask( _ua->Ptr(), nullptr, nullptr, &ctx, &nodeId, nullptr );
		};
		let mayBrowse = [&]( uint userPK ){ return MayBrowse( nodeId, UserPK{(UserPK::Type)userPK} ); };
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

	//open62541 1.5.9 gates every attribute read but Value and RolePermissions on allowBrowseNode, so a user without Read on
	//the root would lose the NodeClass and DataTypeDefinition reads that decoding a permitted value needs.  Outside the
	//Objects tree, type nodes and namespace 0 stay browsable;  inside it every node follows the folder-listing rule - Read
	//on it or beneath it - namespace 0 included (open62541-1.5.9 review #1, #2).
	TEST_F( AccessTests, TypeNodesStayBrowsableWithoutRead ){
		let unknownUser = UserPK{ std::numeric_limits<UserPK::Type>::max() };//no acl row:  no Read anywhere.
		//outside namespace 0 and outside every configured branch, so both fall to the root resource.
		let typeId = UA_NODEID_STRING( 1, (char*)"AccessTests.Type" );
		let objectId = UA_NODEID_STRING( 1, (char*)"AccessTests.Object" );
		ASSERT_EQ( UA_Server_addObjectTypeNode(_ua->Ptr(), typeId, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
			UA_QUALIFIEDNAME(1, (char*)"AccessTestsType"), UA_ObjectTypeAttributes_default, nullptr, nullptr), UA_STATUSCODE_GOOD );
		absl::Cleanup removeType = [&]{ UA_Server_deleteNode( _ua->Ptr(), typeId, true ); };
		ASSERT_EQ( UA_Server_addObjectNode(_ua->Ptr(), objectId, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
			UA_QUALIFIEDNAME(1, (char*)"AccessTestsObject"), typeId, UA_ObjectAttributes_default, nullptr, nullptr), UA_STATUSCODE_GOOD );
		absl::Cleanup removeObject = [&]{ UA_Server_deleteNode( _ua->Ptr(), objectId, true ); };
		//AssignRights collects the type nodes, so MayBrowse reads no node class per call (open62541-1.5.9 review #12) - a type
		//added since it ran is not one yet.  Startup loads every nodeset before AssignRights.
		EXPECT_FALSE( MayBrowse(typeId, unknownUser) ) << "added after AssignRights";
		static_cast<OpcAuthorize&>( *GetSchema().Authorizer ).AssignRights( *_ua );

		EXPECT_TRUE( MayBrowse(typeId, unknownUser) ) << "an ObjectType";
		EXPECT_FALSE( MayBrowse(objectId, unknownUser) ) << "an instance";
		EXPECT_TRUE( MayBrowse(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVERSTATUSDATATYPE), unknownUser) );
		EXPECT_TRUE( MayBrowse(UA_NODEID_NUMERIC(0, UA_NS0ID_TYPESFOLDER), unknownUser) ) << "namespace 0, outside the Objects tree";
		EXPECT_FALSE( MayBrowse(UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), unknownUser) ) << "namespace 0, but the Objects tree's own root:  nothing beneath it is granted";
		let statusNode = criteriaNode();
		EXPECT_FALSE( MayBrowse(statusNode, unknownUser) ) << "namespace 0, but its own resource withholds everything";
		EXPECT_FALSE( MayBrowse(statusNode, UserPK{(UserPK::Type)_users.at("readerUser")}) ) << "Read on the root does not reach past a configured resource";
		EXPECT_EQ( AccessLevel(statusNode, unknownUser), EAccess::None ) << "its value stays gated";
	}

	//open62541-1.5.9 review #2, #4:  browse is a folder listing.  Read on a node lists it, and so does Read on anything
	//configured beneath it, so a user granted one branch can list the path down to it - as r-x on each parent directory
	//does - and 1.5.9's path translation, which asks on every hop, resolves it.  Nodes off that path stay closed, and a
	//user granted nothing lists nothing.
	TEST_F( AccessTests, BrowseFollowsThePathToAGrant ){
		let mayBrowse = [&]( UA_UInt32 node, UserPK user ){ return MayBrowse( UA_NODEID_NUMERIC(0, node), user ); };
		let branch = UserPK{ (UserPK::Type)_users.at("branchUser") };//Read on ServerStatus, nothing on the root.
		EXPECT_TRUE( mayBrowse(UA_NS0ID_SERVER_SERVERSTATUS, branch) ) << "its grant";
		EXPECT_TRUE( mayBrowse(UA_NS0ID_SERVER_SERVERSTATUS_STATE, branch) ) << "inside its grant";
		EXPECT_TRUE( mayBrowse(UA_NS0ID_SERVER, branch) ) << "the parent of its grant";
		EXPECT_TRUE( mayBrowse(UA_NS0ID_OBJECTSFOLDER, branch) ) << "the root of the tree";
		EXPECT_FALSE( mayBrowse(UA_NS0ID_SERVER_SERVERCAPABILITIES, branch) ) << "a sibling of its grant";
		EXPECT_FALSE( mayBrowse(UA_NS0ID_SERVER_NAMESPACEARRAY, branch) );

		let unknownUser = UserPK{ std::numeric_limits<UserPK::Type>::max() };//no acl row:  no Read anywhere.
		EXPECT_FALSE( mayBrowse(UA_NS0ID_OBJECTSFOLDER, unknownUser) );
		EXPECT_FALSE( mayBrowse(UA_NS0ID_SERVER, unknownUser) );

		let reader = UserPK{ (UserPK::Type)_users.at("readerUser") };//Read on the root, everything withheld on ServerStatus.
		EXPECT_TRUE( mayBrowse(UA_NS0ID_SERVER_SERVERCAPABILITIES, reader) );
		EXPECT_FALSE( mayBrowse(UA_NS0ID_SERVER_SERVERSTATUS, reader) );
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

	//The AppServer's read of the live levels for instanceTagLevel's `running` - it answered "Table logSetting not supported",
	//so the column was always null for an OpcServer.
	TEST_F( AccessTests, LogSettingQueryFromTheAppServer ){
		auto q = QL::Parse( "logSetting{ text }", {}, {}, true );//as instanceTagLevel's `running` sends it (QueryInstanceAwait):  raw.
		let y = BlockTAwait<jvalue>( move(*_app->ClientQuery(move(q), Jde::UserPK{})) );
		EXPECT_TRUE( Json::AsObject(y).contains("text") ) << serialize( y );
	}

	//#255:  the instance page's Web Sessions tab, as InstanceSessionsAwait forwards it - raw, over the socket.
	TEST_F( AccessTests, SessionsQueryFromTheAppServer ){
		let session = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.5", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		auto q = QL::Parse( "sessions{ endpoint }", {}, {}, true );
		let y = BlockTAwait<jvalue>( move(*_app->ClientQuery(move(q), _app->UserPK())) );
		ASSERT_TRUE( y.is_array() ) << serialize( y );
		EXPECT_TRUE( std::ranges::any_of(y.as_array(), [](let& s){ return Json::AsString(s.as_object(), "endpoint")=="10.255.0.5"; }) ) << serialize( y );
	}
	//and this server's own ql answers no sessions query:  opcAuthorize holds no `sessions` resource, so a gate here would pass anyone.
	TEST_F( AccessTests, SessionsAreNotThisServersQuery ){
		let session = Web::Server::Sessions::Add( Jde::UserPK{1}, "10.255.0.14", false );
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( session->SessionId ); } };
		EXPECT_THROW( (BlockAwait<QL::QLAwait<jvalue>,jvalue>(QL::QLAwait<jvalue>{"sessions{ endpoint }", {}, Jde::UserPK{}, Server::QLPtr()})), std::exception );
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
	//the OpcServer's own identity.  It is now the opc.<instance>/logs resource's Update, so open to anyone only while that is
	//unenforced, as here.  Nothing here resumes an await, so no level moves and no round trip is made.
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

	TEST_F( CustomMutationTests, AnonymousLogSettingsRoutesWhileLogsIsUnenforced ){
		auto m = mutation( "updateLogSetting" );
		auto y = Server::QL().CustomMutation( m, QL::Creds{UserPK{}}, SRCE_CUR );
		ASSERT_TRUE( y );
		EXPECT_FALSE( y->await_ready() ) << "an enforced resource refuses it 401 - AppQLTests";
	}
	TEST_F( CustomMutationTests, AuthenticatedLogSettingsStillRoutes ){
		auto m = mutation( "updateLogSetting" );//the AppServer's push arrives this way, carrying the admin who made the change.
		auto y = Server::QL().CustomMutation( m, QL::Creds{UserPK{7}}, SRCE_CUR );
		ASSERT_TRUE( y );
		EXPECT_FALSE( y->await_ready() ) << "LogSettingsMAwait suspends; only a refusal answers ready";
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
		EXPECT_NO_THROW( GetUAServer().Trust().LoadTrustList(list) );//it is ι - anything escaping is std::terminate, not a throw the test sees.
		UA_TrustListDataType_clear( &list );

		fs::remove( dangling, ec );
		UA_TrustListDataType after; UA_TrustListDataType_init( &after );
		GetUAServer().Trust().LoadTrustList( after );//resync the mtime cache so the rest of the process sees the real trust list.
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
		GetUAServer().Trust().LoadTrustList( before );
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
		GetUAServer().Trust().LoadTrustList( list );
		EXPECT_EQ( list.trustedCertificatesSize, baseline+1 ) << "the .der was passed over";
		UA_TrustListDataType_clear( &list );

		fs::remove( staged, ec );
		fs::remove_all( scratch, ec );
		UA_TrustListDataType after; UA_TrustListDataType_init( &after );
		GetUAServer().Trust().LoadTrustList( after );//resync the mtime cache - the rest of the process must see the real trust list.
		EXPECT_EQ( after.trustedCertificatesSize, baseline );
		UA_TrustListDataType_clear( &after );
	}

	//reviews/m2-closing.md #11:  the scan's "passed over" Warning fired only when the *whole* trust list came up empty, and an
	//installed OpcServer's never does - its one trusted dir holds the hub's own certificate before the server starts - so a
	//file ignored beside it was a Debug line and nothing else.  This directory is that case:  the suite's own client
	//certificate is already anchored in it.  The file is named at Warning the first time the scan passes it over and at Debug
	//from then on - a failed verify rescans, so louder every time would repeat under a junk-certificate flood.
	TEST( TrustListTests, APassedOverFileIsNamedOnceBesideARealAnchor ){
		let dirs = Settings::FindStringArray( "/access/trustedCertDirs" );
		ASSERT_FALSE( dirs.empty() );
		const fs::path dir{ dirs.front() };
		let staged = dir/Ƒ( "m2-closing-11-{}.pfx", Process::ProcessId() );//a name no earlier scan in this process has seen.
		let junk = string{ "a PKCS#12 bundle, as far as its name goes" };
		IO::SaveBinary<const char>( staged, std::span{junk} );

		Logging::ClearMemory();
		uint anchored{};
		for( uint scan=0; scan<3; ++scan ){
			UA_TrustListDataType list; UA_TrustListDataType_init( &list );
			GetUAServer().Trust().LoadTrustList( list );
			anchored = list.trustedCertificatesSize;
			UA_TrustListDataType_clear( &list );
		}
		std::error_code ec; fs::remove( staged, ec );
		ASSERT_NE( anchored, 0u ) << "the point is a list that is NOT empty - the suite's own client certificate should be anchored here";

		let named = Logging::Find( [&]( const Logging::Entry& e ){ return e.Message().contains(staged.string()); } );
		ASSERT_EQ( named.size(), 3u ) << "one line per scan";
		EXPECT_EQ( named[0].Level, ELogLevel::Warning ) << "passed over beside a real anchor, and only Debug said so";
		EXPECT_EQ( named[1].Level, ELogLevel::Debug );
		EXPECT_EQ( named[2].Level, ELogLevel::Debug );
	}

	//security-matrix #5:  there is no unsecured shape.  A config with no /opcServer/ssl - a hidden `ssl::`, a mistyped key - used to
	//build a None-only server with no certificate and no trust list;  now the server refuses to start, and names the setting.
	//The host refuses, not UAConfig, which sees a CryptoSettings either way:  an empty block is the default paths.
	TEST( ReadUASettingsTests, NoSslIsRefused ){
		let ssl = Settings::FindObject( "/opcServer/ssl" );
		ASSERT_TRUE( ssl ) << "the suite's own server runs secured";
		const jobject original{ *ssl };
		absl::Cleanup restore = [&original]{ try{ Settings::Set("/opcServer/ssl", original); }catch( const std::exception& ){} };
		Settings::Set( "/opcServer/ssl", jvalue{} );//null - FindObject answers nullptr for it, as for an absent key.
		ASSERT_FALSE( Settings::FindObject("/opcServer/ssl") );
		try{
			ReadUASettings();
			FAIL() << "the UA server's settings were read without /opcServer/ssl";
		}
		catch( const std::exception& e ){
			EXPECT_TRUE( string{e.what()}.contains("/opcServer/ssl") ) << e.what();
		}
	}

	//A host's null appClient or nodeAccess is refused at construction, not dereferenced by the first session's callback.
	TEST( UAConfigTests, NullHostInterfacesAreRefused ){
		let nodeAccess = dynamic_pointer_cast<OpcAuthorize>( GetSchema().Authorizer );
		ASSERT_TRUE( nodeAccess );
		let refused = []( sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, sv name ){
			try{
				UAConfig config{ ReadUASettings(), move(appClient), move(nodeAccess) };
				ADD_FAILURE() << "a server config was built with a null " << name;
			}
			catch( const std::exception& e ){
				EXPECT_TRUE( string{e.what()}.contains(name) ) << e.what();
			}
		};
		refused( AppClient(), nullptr, "nodeAccess" );
		refused( nullptr, nodeAccess, "appClient" );
	}

	//servers spec *Limits*:  none:false keeps the None policy, for discovery alone, and offers no endpoint on it - the flag
	//set, every endpoint secured.  The suite's one server has none:true, so the config is built and never run.
	TEST( UAConfigTests, NoneOffKeepsThePolicyForDiscoveryAlone ){
		let nodeAccess = dynamic_pointer_cast<OpcAuthorize>( GetSchema().Authorizer );
		ASSERT_TRUE( nodeAccess );
		auto settings = ReadUASettings();
		settings.None = false;
		UAConfig config{ settings, AppClient(), nodeAccess };
		EXPECT_TRUE( config.securityPolicyNoneDiscoveryOnly );
		bool nonePolicy{};
		for( uint i=0; i<config.securityPoliciesSize; ++i )
			nonePolicy = nonePolicy || UA_String_equal( &config.securityPolicies[i].policyUri, &UA_SECURITY_POLICY_NONE_URI );
		EXPECT_TRUE( nonePolicy ) << "the policy stays, for GetEndpoints";
		ASSERT_GT( config.endpointsSize, 0u );
		for( uint i=0; i<config.endpointsSize; ++i ){
			EXPECT_FALSE( UA_String_equal(&config.endpoints[i].securityPolicyUri, &UA_SECURITY_POLICY_NONE_URI) );
			EXPECT_NE( config.endpoints[i].securityMode, UA_MESSAGESECURITYMODE_NONE );
		}
		UA_ServerConfig_clear( &config );
	}

	//reviews-issue/servers-2-endpoint.md #8:  a session per channel, so a maxSessions past open62541's 100 channels raises them
	//with it;  one under leaves them.
	TEST( UAConfigTests, MaxSessionsRaisesTheChannelCap ){
		let nodeAccess = dynamic_pointer_cast<OpcAuthorize>( GetSchema().Authorizer );
		ASSERT_TRUE( nodeAccess );
		let channels = [&]( uint16 maxSessions ){
			auto settings = ReadUASettings();
			settings.Limit.MaxSessions = maxSessions;
			UAConfig config{ settings, AppClient(), nodeAccess };
			EXPECT_EQ( config.maxSessions, maxSessions );
			let y = config.maxSecureChannels;
			UA_ServerConfig_clear( &config );
			return y;
		};
		EXPECT_EQ( channels(250), 250 );
		EXPECT_EQ( channels(10), 100 ) << "open62541's own, kept";
	}

	//reviews-issue/servers-2-endpoint.md #4:  the memory stores' rejected lists are capped, where setBasics' 0 left them unbounded.
	TEST( UAConfigTests, TheRejectedListIsCapped ){
		let nodeAccess = dynamic_pointer_cast<OpcAuthorize>( GetSchema().Authorizer );
		ASSERT_TRUE( nodeAccess );
		UAConfig config{ ReadUASettings(), AppClient(), nodeAccess };
		EXPECT_EQ( config.maxRejectedListSize, UATrust::MaxRejected );
		UA_ServerConfig_clear( &config );
	}

	//install-issues "Noise in a production log":  a trusted dir that is not there - a product not installed, or not started
	//yet - is one Warning, not one per scan; a failed verify rescans, so it used to repeat for the life of the process.
	//Later sightings are Debug.  The dir is appended to the real list in a trust of the test's own, so the running server's
	//is untouched.
	TEST( TrustListTests, AMissingDirectoryWarnsOnce ){
		let dirs = Settings::FindStringArray( "/access/trustedCertDirs" );
		ASSERT_FALSE( dirs.empty() );
		let missing = fs::path{ dirs.front() }.parent_path()/Ƒ( "missing-{}", Process::ProcessId() );//never created - and a name no earlier scan has seen.
		vector<fs::path> withMissing{ dirs.begin(), dirs.end() };
		withMissing.push_back( missing );
		UATrust trust{ move(withMissing) };
		Logging::ClearMemory();
		for( uint i=0; i<3; ++i ){
			UA_TrustListDataType list; UA_TrustListDataType_init( &list );
			trust.LoadTrustList( list );
			UA_TrustListDataType_clear( &list );
		}

		let lines = Logging::Find( [&](const Logging::Entry& e){ return e.Text.contains("does not exist") && e.Message().contains(missing.string()); } );
		ASSERT_EQ( lines.size(), 3u ) << "one line per scan";
		EXPECT_EQ( lines[0].Level, ELogLevel::Warning );
		EXPECT_EQ( lines[1].Level, ELogLevel::Debug );
		EXPECT_EQ( lines[2].Level, ELogLevel::Debug );
	}

	//opcserver-review3 L24:  every accepting branch of ActivateSession assigned straight through *sessionContext, and
	//open62541 passes &session->context on *every* activation - re-activation is explicitly allowed for the same user, and
	//the vendor's own client re-activates on a channel renew - while closeSession only ever sees the last pointer.  So each
	//re-activation dropped the previous context on the floor.  Nothing but the gateway suite drives activation at all, and
	//nothing drives it twice on one session;  this does.
	TEST_F( AccessTests, ReactivatingASessionReplacesItsContext ){
		let jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		IssuedIdentity identity{ jwt.Payload() };//the wire form the gateway sends (TokenTests does the same).
		void* slot{};
		ASSERT_EQ( identity.Activate(slot), UA_STATUSCODE_GOOD );
		let first = slot; ASSERT_TRUE( first );
		EXPECT_EQ( static_cast<UAAccess::SessionContext*>(slot)->UserPK, AppClient()->UserPK() );

		ASSERT_EQ( identity.Activate(slot), UA_STATUSCODE_GOOD ) << "re-activation is allowed and must not be refused";
		EXPECT_TRUE( slot );
		EXPECT_EQ( static_cast<UAAccess::SessionContext*>(slot)->UserPK, AppClient()->UserPK() ) << "the replacement carries the same identity";
		//`first` is freed by the second activation - the fix.  Not asserted: the free itself is not observable here, since
		//Process::Shutdown ends in std::_Exit and LSan never runs, and a recoverable leak check would trip on the suite's
		//pre-existing ones (opcserver-review #23).
		UAAccess::CloseSession( _ua->Ptr(), nullptr, nullptr, slot );
	}

	//open62541 1.5.9 refuses an identity change on re-activation, but compares an issued token's user id, which it leaves
	//empty - so a session activated as one user and re-activated with another user's token reaches ActivateSession, which
	//must refuse it and keep the session's context rather than swap in the new user.
	TEST_F( AccessTests, ReactivatingAsAnotherUserIsRefused ){
		let jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		IssuedIdentity identity{ jwt.Payload() };
		let readerPK = UserPK{ (UserPK::Type)_users.at("readerUser") };
		ASSERT_NE( readerPK, AppClient()->UserPK() );
		void* slot = UAAccess::SessionContext::Unexpiring( readerPK ).release();//the session as first activated, by the reader.
		let first = slot;
		EXPECT_EQ( identity.Activate(slot), UA_STATUSCODE_BADIDENTITYCHANGENOTSUPPORTED );
		EXPECT_EQ( slot, first ) << "the session keeps its context";
		EXPECT_EQ( static_cast<UAAccess::SessionContext*>(slot)->UserPK, readerPK );
		UAAccess::CloseSession( _ua->Ptr(), nullptr, nullptr, slot );
	}

	//security-review #1:  an issued token of 9+ bytes is parsed as a jwt, and a malformed n claim in it terminated the OpcServer.
	TEST_F( AccessTests, AnIssuedTokenWithAMalformedKeyClaimIsRefused ){
		let head = jobject{ {"alg","RS256"}, {"typ","JWT"} };
		let body = jobject{ {"iat", time(nullptr)}, {"n", "!!!"}, {"e", "AQAB"} };
		let token = Str::Encode64( serialize(head), true )+"."+Str::Encode64( serialize(body), true )+"."+Str::Encode64( "notVerifiedHere"s, true );
		IssuedIdentity identity{ token };
		void* slot{};
		EXPECT_EQ( identity.Activate(slot), UA_STATUSCODE_BADIDENTITYTOKENINVALID );
		EXPECT_FALSE( slot );
	}

	//opcserver-review3 L20:  the session-expiry renewal was a BlockAwait made from inside the access-control callbacks -
	//which open62541 calls with its serviceMutex held - so a hung AppServer froze every OPC client for the socket deadline,
	//and the timeout then tore down the app-client socket.  It is now posted and answered on the io thread; the UA thread
	//serves the lapsed snapshot while the question is outstanding, bounded by one renewal interval, and any answer -
	//including a failure - ends that grace.  These drive GetUserAccessLevel, which is what consults it.
	TEST_F( AccessTests, ALapsedSessionIsServedWhileTheRenewalIsOutstandingThenDenied ){
		let nodeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVER );
		const UserPK readerPK{ (UserPK::Type)_users.at("readerUser") };
		let access = [&]( UAAccess::SessionContext& ctx ){ return AccessLevel( nodeId, ctx ); };

		//No session id: nothing to ask the authority about, so a lapsed snapshot is simply expired.
		auto noId = Session( readerPK, Clock::now()-1s );
		EXPECT_EQ( access(noId), EAccess::None );

		//Lapsed well past the renewal interval: outstanding or not, the snapshot is too old to serve.
		auto stale = Session( readerPK, Clock::now()-10min, 4242 );
		EXPECT_EQ( access(stale), EAccess::None );

		//Just lapsed, renewal outstanding: served rather than denied - and it returned, which is the whole point.
		auto lapsed = Session( readerPK, Clock::now()-1s, 4243 );
		let start = Clock::now();
		EXPECT_EQ( access(lapsed), ToAccess(_readerAllowed) );
		EXPECT_LT( Clock::now()-start, 5s ) << "the callback must not wait on the AppServer";

		//4243 is not a session the AppServer knows, so the renewal fails - and a failure is an answer, which ends the grace.
		for( uint i=0; i<100 && access(lapsed)!=EAccess::None; ++i )
			std::this_thread::sleep_for( 50ms );
		EXPECT_EQ( access(lapsed), EAccess::None ) << "once the authority has answered, the lapsed snapshot is no longer served";
	}

	//soak-findings #13:  the renewal above was asked only once the snapshot had lapsed - the instant the authority's own copy
	//lapses too, the snapshot being a copy of it - and an expired session is never revived.  So it could not renew a session
	//nothing else was sliding, and an OPC session in continuous use through the gateway was denied a day in.  It is now
	//asked ahead, at the snapshot's half-life, while the ask still slides the session.
	TEST_F( AccessTests, ASessionIsRenewedAheadOfItsLapse ){
		let nodeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVER );
		const UserPK readerPK{ (UserPK::Type)_users.at("readerUser") };
		let access = [&]( UAAccess::SessionContext& ctx ){ return AccessLevel( nodeId, ctx ); };

		auto fresh = Session( readerPK, Clock::now()+1h, 4244 );
		EXPECT_GT( fresh.RenewAt, Clock::now()+29min ) << "asked at half-life, not before";
		EXPECT_LT( fresh.RenewAt, fresh.Expiration ) << "and not at the lapse, which is too late";
		EXPECT_EQ( Session(readerPK).RenewAt, TimePoint::max() ) << "no session, nothing to ask about";

		//The suite's own app session - one the AppServer knows - a minute from lapsing here, its half-life reached.
		auto live = Session( readerPK, Clock::now()+1min, AppClient()->SessionId() );
		ASSERT_TRUE( live.SessionId );
		live.RenewAt = Clock::now()-1s;
		let snapshot = live.Expiration;
		EXPECT_EQ( access(live), ToAccess(_readerAllowed) ) << "still good, so served - and the ask is posted";
		for( uint i=0; i<100 && live.Expiration==snapshot; ++i ){
			std::this_thread::sleep_for( 50ms );
			access( live );
		}
		EXPECT_GT( live.Expiration, snapshot ) << "the authority's answer extends the snapshot before it lapses";
		EXPECT_GT( live.RenewAt, Clock::now() ) << "and the next ask waits for the new half-life";

		//4245 is not a session the AppServer knows, so the ask fails.  At the lapse that is a verdict;  ahead of it the snapshot
		//is still the authority's own word, and a failure - an AppServer restart would be one - must not cut it short.
		auto unknown = Session( readerPK, Clock::now()+1min, 4245 );
		unknown.RenewAt = Clock::now()-1s;
		let good = unknown.Expiration;
		for( uint i=0; i<100 && unknown.RenewAt<=Clock::now(); ++i ){
			EXPECT_EQ( access(unknown), ToAccess(_readerAllowed) );
			std::this_thread::sleep_for( 50ms );
		}
		EXPECT_GT( unknown.RenewAt, Clock::now() ) << "the failure was collected, and the next ask backs off";
		EXPECT_EQ( unknown.Expiration, good ) << "and left the snapshot alone";
		EXPECT_EQ( access(unknown), ToAccess(_readerAllowed) );
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
		EXPECT_EQ( auth.NodeRights(serverNode, reader), _readerAllowed ) << "a node resource the reader holds nothing on takes nothing from the root's grant (reviews-issue/252-effective-rights-group.md #1)";
		{//mapped, which shows with the root unenforced:  the rest of the tree is open, and the node's own resource protects it.
			const Access::ResourcePK rootPK{ Json::AsNumber<Access::ResourcePK::Type>(_app->QuerySync("resource( schemaName:$schema, slug:\"nodeIds\", criteria:null ){id}", {{"schema", _resource}}), "id") };
			auth.UpdateResourceDeleted( rootPK, _resource, jobject{{"id",rootPK.Value}}, false );
			absl::Cleanup restore = [&]{ auth.UpdateResourceDeleted( rootPK, _resource, jobject{{"id",rootPK.Value}}, true ); };
			let stranger = UserPK{ std::numeric_limits<UserPK::Type>::max() };
			EXPECT_EQ( auth.NodeRights(NodeId::ObjectsFolder(), stranger), Access::ERights::All ) << "the root unenforced, the tree is open";
			EXPECT_EQ( auth.NodeRights(serverNode, stranger), Access::ERights::None ) << "after: its own resource protects the node";
			EXPECT_EQ( auth.NodeRights(serverNode, reader), _readerAllowed ) << "and the unenforced root's grant still counts under it";
		}
		EXPECT_EQ( auth.NodeRights(NodeId::ObjectsFolder(), reader), _readerAllowed ) << "the root is untouched";

		auth.UpdateResourceDeleted( scratchPK, _resource, jobject{{"id",scratchPK.Value}}, false );
		EXPECT_EQ( auth.NodeRights(serverNode, reader), _readerAllowed ) << "deleting it hands the node back to the root, also without a restart";
	}

	//reviews-issue/252-effective-rights-group.md #1:  a grant on a node adds to the root's and takes nothing from it.  Another
	//role's grant there leaves the reader's root Read standing, and a role holding Read on the root and Update on the node has
	//both there.  ServerCapabilities, which no other test grants on;  the grants arrive through the AccessListener, so waited for.
	TEST_F( AccessTests, ANodeGrantAddsToTheRoot ){
		auto& auth = static_cast<OpcAuthorize&>( *GetSchema().Authorizer );
		const NodeId node{ UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERCAPABILITIES) };
		let userPK = FindOrCreateUser( *_app, "additiveUser" );
		let criteria = node.ToString();
		GrantRole( *_app, roleSlug("additive"), userPK, {{_resource, "nodeIds", ERights::Read}, {_resource, "nodeIds", ERights::Update, {}, criteria, "ServerCapabilities"}} );
		constexpr ERights both = ERights::Read | ERights::Update;
		for( uint i=0; i<100 && auth.NodeRights(node, userPK)!=both; ++i )
			std::this_thread::sleep_for( 50ms );
		EXPECT_EQ( auth.NodeRights(node, userPK), both ) << "the node's Update on top of the root's Read";
		EXPECT_EQ( auth.NodeRights(NodeId::ObjectsFolder(), userPK), ERights::Read ) << "the node's grant stays on the node";
		let reader = UserPK{ (UserPK::Type)_users.at("readerUser") };
		EXPECT_EQ( auth.NodeRights(node, reader), _readerAllowed ) << "another role's grant on the node leaves the reader's root grant standing";
	}
}
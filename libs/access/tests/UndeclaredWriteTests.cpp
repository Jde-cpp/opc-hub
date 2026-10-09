//GHSA-g354-grf2-r8vh:  the tables that define rights - identities, providers, permissions, permissionRights, roleMembers,
//rights, seeds - declare ops:["None"], and resources declares Delete alone.  Table::Authorize tested only the rights a table
//declares, so a generic create/update/delete/purge on them was refused to no one - the unauthenticated UserPK{0} included -
//and ResourceLoadAwait makes no resource row for an undeclared op, so enforcing every resource could not close it.  Such a
//write is the system's alone now (IAcl::TestSystem), a read outside the ops any known user's (IAcl::TestUser), and an
//extension's purge still covers the row it extends.  A few have callers that are not the system and take an admin check instead (AdminWriteAwait.h):
//updatePermissionRight, resources' create/update and providers' create/purge.
#include "gtest/gtest.h"
#include <jde/access/AccessException.h>
#include <jde/access/Authorize.h>
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	struct UndeclaredWriteTests : ::testing::Test{
		Ω SetUpTestCase()->void{ _intruder = UserPK{ GetId(Tests::Get("user", "ghsa-g354-intruder", GetRoot())) }; }
		Ω TearDownTestCase()->void{ PurgeUser( _intruder, GetRoot() ); }
		//Refused by the gate and nothing else:  every generic op authorizes before it reads its args, and a parse or data failure
		//answers 400/500, so the status is what tells a refusal from a mutation that merely failed.
		Ω expectRefused( str ql, UserPK executer, EHttpStatus status )->void{
			try{
				QL().QuerySync<jvalue>( ql, {}, executer );
				ADD_FAILURE() << "accepted for executer " << executer.Value << ": " << ql;
			}
			catch( const Exception& e ){
				EXPECT_EQ( e.HttpStatus(), status ) << ql << " - " << e.what();
			}
		}
		static UserPK _intruder;
	};
	UserPK UndeclaredWriteTests::_intruder{};

	//One write of each generic kind per table, as a client would send it.  The anonymous caller is unknown (401), the intruder a
	//user with no grant on anything (403) - the advisory's two callers.
	TEST_F( UndeclaredWriteTests, AnonymousAndNonAdminAreRefused ){
		//no row has these keys, bar the intruder's own:  a gate that regressed changes nothing in the shared db (review #10).
		let id = _intruder.Value;
		constexpr uint missing{ 4'000'000'000 };
		constexpr uint missingRight{ 3 };//rights ids are single bits.
		const vector<string> writes{
			"mutation createIdentity( name:\"ghsa-g354\", slug:\"ghsa-g354\" )",
			Ƒ( "mutation updateIdentity( id:{}, description:\"ghsa-g354\" )", id ),
			Ƒ( "mutation deleteIdentity( id:{} )", id ),
			Ƒ( "mutation purgeIdentity( id:{} )", id ),
			Ƒ( "mutation updateProvider( id:{}, slug:\"ghsa-g354\" )", missing ),
			Ƒ( "mutation deleteProvider( id:{} )", missing ),
			"mutation createPermission( isRole:false )",
			Ƒ( "mutation updatePermission( id:{}, isRole:true )", missing ),
			Ƒ( "mutation purgePermission( id:{} )", missing ),
			Ƒ( "mutation createPermissionRight( permissionId:{0}, resourceId:{0}, allowed:255, denied:0 )", missing ),
			Ƒ( "mutation purgePermissionRight( id:{} )", missing ),
			Ƒ( "mutation createRoleMember( roleId:{0}, memberId:{0} )", missing ),
			Ƒ( "mutation addRoleMember( id:{0}, memberId:[{0}] )", missing ),
			Ƒ( "mutation removeRoleMember( id:{0}, memberId:[{0}] )", missing ),
			Ƒ( "mutation purgeRoleMember( id:{} )", missing ),
			"mutation createRights( id:8, name:\"ghsa-g354\" )",//Delete's id:  a regressed insert is a duplicate.
			Ƒ( "mutation updateRights( id:{}, name:\"ghsa-g354\" )", missingRight ),
			Ƒ( "mutation purgeRights( id:{} )", missingRight ),
			"mutation createSeeds( name:\"ghsa-g354\", contentHash:\"0\", applied:\"2026-10-09T00:00:00Z\" )",//plural, as rights:  Names::IsPlural reads the -ed of `seed` as a plural, so the singular resolves no table.
			"mutation updateSeeds( name:\"ghsa-g354\", contentHash:\"0\" )",
			Ƒ( "mutation purgeResource( id:{} )", missing )//create/update: ResourceWritesNeedTheSchemasAdmin.  Providers' create/purge: ProviderWritesNeedTheUsersAdmin.
		};
		for( let& ql : writes ){
			expectRefused( ql, UserPK{}, EHttpStatus::Unauthorized );
			expectRefused( ql, _intruder, EHttpStatus::Forbidden );
		}
		//and no insert landed.
		let root = GetRoot();
		EXPECT_TRUE( QL().QuerySync("identity( slug:\"ghsa-g354\" ){ id }", {}, root).empty() );
		EXPECT_TRUE( QL().QuerySync("provider( name:\"ghsa-g354\" ){ id }", {}, root).empty() );
		EXPECT_TRUE( QL().QuerySync<jarray>("seeds( name:\"ghsa-g354\" ){ name }", {}, root).empty() );
	}

	//updatePermissionRight is the Permissions tab's save, so it is not the system's alone:  Server::CustomMutation routes it
	//through an Administer check on the grant's resource - the right that drops the grant - before the stock update runs.
	TEST_F( UndeclaredWriteTests, UpdatePermissionRightNeedsAdministerOnItsResource ){
		let root = GetRoot();
		const string resource{ "groups" };
		RestoreResource( resource, root );//enforced, so the check is live.
		const UserPK holder{ GetId(GetUser("ghsa-g354-holder", root)) };
		let permissionPK = CreateAcl( holder, ERights::Read, ERights::None, resource, root );
		let update = Ƒ( "mutation updatePermissionRight( id:{}, allowed:255 )", permissionPK.Value );
		expectRefused( update, UserPK{}, EHttpStatus::Unauthorized );
		expectRefused( update, _intruder, EHttpStatus::Forbidden );
		EXPECT_EQ( Authorizer()->Rights("access", resource, holder), ERights::Read ) << "a refused update changed nothing";
		//not even an admin moves a grant:  the check covered `groups` only (review #1).
		let resourceOf = [&]{ return Json::AsNumber<ResourcePK::Type>( Json::AsObject(QL().QuerySync<jarray>(Ƒ("permissionRights( id:{} ){{ resource{{ id }} }}", permissionPK.Value), {}, root).at(0)), "resource/id" ); };
		let groupsPK = resourceOf();
		let users = GetId( SelectResource("users", root, true) );
		expectRefused( Ƒ("mutation updatePermissionRight( id:{}, resourceId:{}, allowed:255 )", permissionPK.Value, users), root, EHttpStatus::BadRequest );
		EXPECT_EQ( resourceOf(), groupsPK );
		expectRefused( "mutation updatePermissionRight( id:4000000000, allowed:255 )", root, EHttpStatus::NotFound );
		EXPECT_EQ( Authorizer()->Rights("access", resource, holder), ERights::Read ) << "a refused update changed nothing";
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(update, {}, root) );//root administers every resource.
		EXPECT_EQ( Authorizer()->Rights("access", resource, holder), ERights::All ) << "and the admin's reached the cache";
		//unenforced, the admin check passes anyone - but not a caller nobody knows (review #2).
		Delete( "resources", groupsPK, root );
		expectRefused( Ƒ("mutation updatePermissionRight( id:{}, allowed:0 )", permissionPK.Value), UserPK{}, EHttpStatus::Unauthorized );
		RestoreResource( resource, root );
		EXPECT_EQ( Authorizer()->Rights("access", resource, holder), ERights::All );
		PurgeAcl( holder, permissionPK, root );
		PurgeUser( holder, root );
	}

	//createResource/updateResource are every instance's resource sync, run as the instance's login, so they are not the system's
	//alone:  a known user who administers the row's schema - on an update, the schema it is in and any it moves to.  A schema
	//with no enforced root passes, as an instance's own does on a fresh install.
	TEST_F( UndeclaredWriteTests, ResourceWritesNeedTheSchemasAdmin ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		RestoreResource( "groups", root );//an enforced access root, so administering `access` means something.
		let groups = GetId( SelectResource("groups", root) );
		let createInAccess = Ƒ( R"(mutation createResource( schemaName:"access", name:"{0}", slug:"{0}" ))", "ghsa-g354-r" );
		let updateGroups = Ƒ( "mutation updateResource( id:{}, description:\"ghsa-g354\" )", groups );
		for( let& ql : {createInAccess, updateGroups} ){
			expectRefused( ql, UserPK{}, EHttpStatus::Unauthorized );
			expectRefused( ql, _intruder, EHttpStatus::Forbidden );
		}
		EXPECT_TRUE( Select("resource", "ghsa-g354-r", root, "id", true).empty() );
		EXPECT_EQ( Json::AsString(Select("resource", groups, root, "description", true), "description"), "From installation" );

		//the sync's own shape, in a schema nobody enforces:  create, then disable;  a later update to the disabled row.
		constexpr sv schema{ "ghsa-g354-schema" };
		for( let& v : QL().QuerySync<jarray>(Ƒ("resources( schemaName:\"{}\" ){{ id deleted }}", schema), {}, root) )//a previous run's.
			Purge( "resource", GetId(Json::AsObject(v)), system );
		let id = GetId( QL().QuerySync(Ƒ(R"(mutation createResource( schemaName:"{0}", name:"{0}", slug:"{0}" ){{ id }})", schema), {}, _intruder) );
		Delete( "resources", id, root );
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation updateResource( id:{}, description:\"ghsa-g354\" )", id), {}, _intruder) );
		expectRefused( Ƒ("mutation updateResource( id:{}, description:\"ghsa-g354\" )", id), UserPK{}, EHttpStatus::Unauthorized );

		//moving it into a schema the intruder does not administer is refused;  into another nobody enforces, it is not.  (Not root
		//into access for the positive half:  the suite's other tests leave enforced access roots root holds nothing on.)
		expectRefused( Ƒ("mutation updateResource( id:{}, schemaName:\"access\" )", id), _intruder, EHttpStatus::Forbidden );
		EXPECT_EQ( Json::AsString(Select("resource", id, root, "schemaName", true), "schemaName"), schema );
		constexpr sv other{ "ghsa-g354-other" };
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation updateResource( id:{}, schemaName:\"{}\" )", id, other), {}, _intruder) );
		EXPECT_EQ( Json::AsString(Select("resource", id, root, "schemaName", true), "schemaName"), other );
		Purge( "resource", id, system );
	}

	//An enforced criteria row - an OPC branch protected while its slug's root stays open - is no root, so TestSchemaAdmin never
	//saw it:  retargeting it, or moving it to another schema, opened the branch (review #3).
	TEST_F( UndeclaredWriteTests, AnEnforcedCriteriaRowNeedsItsOwnAdmin ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		constexpr sv schema{ "ghsa-g354-branch" };
		for( let& v : QL().QuerySync<jarray>(Ƒ("resources( schemaName:\"{}\" ){{ id deleted }}", schema), {}, root) )//a previous run's.
			Purge( "resource", GetId(Json::AsObject(v)), system );
		let rootRow = GetId( QL().QuerySync(Ƒ(R"(mutation createResource( schemaName:"{}", name:"nodes", slug:"nodes" ){{ id }})", schema), {}, system) );
		Delete( "resources", rootRow, root );
		let branch = GetId( QL().QuerySync(Ƒ(R"(mutation createResource( schemaName:"{}", name:"nodes", slug:"nodes", criteria:"branch" ){{ id }})", schema), {}, system) );
		for( let& ql : {Ƒ("mutation updateResource( id:{}, criteria:\"other\" )", branch), Ƒ("mutation updateResource( id:{}, schemaName:\"junk\" )", branch)} )
			expectRefused( ql, _intruder, EHttpStatus::Forbidden );
		let row = Select( "resource", branch, root, "schemaName criteria", true );
		EXPECT_EQ( Json::AsString(row, "schemaName"), schema );
		EXPECT_EQ( Json::AsString(row, "criteria"), "branch" );
		Delete( "resources", branch, root );//unenforced, it is as open as any other row.
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation updateResource( id:{}, description:\"ghsa-g354\" )", branch), {}, _intruder) );
		Purge( "resource", branch, system );
		Purge( "resource", rootRow, system );
	}

	//The positive half of the resource gates where they bite (review #11):  a user granted Administer on an enforced root writes the
	//schema's rows, and on an enforced criteria row once granted that row too - the intruder, holding nothing, writes neither.
	TEST_F( UndeclaredWriteTests, AnEnforcedSchemasAdminWritesItsRows ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		constexpr sv schema{ "ghsa-g354-admin" };
		for( let& v : QL().QuerySync<jarray>(Ƒ("resources( schemaName:\"{}\" ){{ id deleted }}", schema), {}, root) )//a previous run's.
			Purge( "resource", GetId(Json::AsObject(v)), system );
		let create = [&]( sv slug, sv criteria, UserPK executer ){
			return GetId( QL().QuerySync(Ƒ(R"(mutation createResource( schemaName:"{0}", name:"{1}", slug:"{1}"{2} ){{ id }})", schema, slug, criteria), {}, executer) );
		};
		let rootRow = create( "nodes", {}, system );//the system's creates stay enforced.
		let branch = create( "nodes", ", criteria:\"branch\"", system );
		const UserPK admin{ GetId(GetUser("ghsa-g354-schema-admin", root)) };
		let grant = [&]( uint resource ){
			let y = QL().QuerySync( Ƒ("mutation createAcl( identity:{{ id:{} }}, permissionRight:{{ allowed:{}, denied:0, resource:{{ id:{} }} }} ){{ permissionRight{{ id }} }}", admin.Value, underlying(ERights::Administer), resource), {}, system );
			return PermissionPK{ Json::AsNumber<PermissionPK::Type>(y, "permissionRight/id") };
		};
		let rootGrant = grant( rootRow );

		let createLeaf = Ƒ( R"(mutation createResource( schemaName:"{}", name:"leaf", slug:"leaf" ){{ id }})", schema );
		let updateRoot = Ƒ( "mutation updateResource( id:{}, description:\"ghsa-g354\" )", rootRow );
		let moveBranch = Ƒ( "mutation updateResource( id:{}, criteria:\"moved\" )", branch );
		for( let& ql : {createLeaf, updateRoot, moveBranch} )
			expectRefused( ql, _intruder, EHttpStatus::Forbidden );
		let leaf = GetId( QL().QuerySync(createLeaf, {}, admin) );
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(updateRoot, {}, admin) );
		EXPECT_EQ( Json::AsString(Select("resource", rootRow, root, "description", true), "description"), "ghsa-g354" );
		expectRefused( moveBranch, admin, EHttpStatus::Forbidden );//the root's admin, not the branch's.
		let branchGrant = grant( branch );
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(moveBranch, {}, admin) );
		EXPECT_EQ( Json::AsString(Select("resource", branch, root, "criteria", true), "criteria"), "moved" );

		PurgeAcl( admin, branchGrant, system );
		PurgeAcl( admin, rootGrant, system );
		for( let id : {leaf, branch, rootRow} )
			Purge( "resource", id, system );
		PurgeUser( admin, root );
	}

	//A non-system create lands unenforced, as the sync's create-then-disable leaves it.  Enforced, it was a root nobody administers:
	//everyone but the system was locked out of it, and its creator out of the schema's next write (review #7).
	TEST_F( UndeclaredWriteTests, ANonSystemCreateIsUnenforced ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		constexpr sv schema{ "ghsa-g354-created" };
		for( let& v : QL().QuerySync<jarray>(Ƒ("resources( schemaName:\"{}\" ){{ id deleted }}", schema), {}, root) )//a previous run's.
			Purge( "resource", GetId(Json::AsObject(v)), system );
		let create = [&]( sv slug ){ return GetId( QL().QuerySync(Ƒ(R"(mutation createResource( schemaName:"{0}", name:"{1}", slug:"{1}" ){{ id }})", schema, slug), {}, _intruder) ); };
		let first = create( "first" );
		EXPECT_FALSE( Select("resource", first, root, "id", true).at("deleted").is_null() );
		EXPECT_EQ( Authorizer()->Rights(string{schema}, "first", _intruder), ERights::All ) << "not enforced";
		let second = create( "second" );//the creator still administers the schema.
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation updateResource( id:{}, description:\"ghsa-g354\" )", first), {}, _intruder) );
		Purge( "resource", first, system );
		Purge( "resource", second, system );
	}

	//createProvider/purgeProvider are a standalone gateway's, run as its own login when it adds or purges an OpcServer connection
	//(the hub's run as the system):  a known user who administers `users`, since a purge takes the provider's identities with it.
	TEST_F( UndeclaredWriteTests, ProviderWritesNeedTheUsersAdmin ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		constexpr auto select = "provider( name:\"ghsa-g354-p\" ){ id }";
		if( let leftover = QL().QuerySync(select, {}, root); !leftover.empty() )//a previous run's.
			Purge( "provider", GetId(leftover), system );
		const UserPK admin{ GetId(GetUser("ghsa-g354-users-admin", root)) };
		let usersPK = GetId( SelectResource("users", root, true) );
		let wasEnforced = SelectResource( "users", root, true ).at( "deleted" ).is_null();
		RestoreResource( "users", system );
		let grant = CreateAcl( admin, ERights::Administer, ERights::None, "users", system );

		const string create{ "mutation createProvider( slug:\"ghsa-g354-p\", providerType:\"OpcServer\" ){ id }" };//the gateway's own shape.
		for( let& ql : {create, string{"mutation purgeProvider( id:4000000000 )"}} ){//no such row:  a gate that regressed purges nothing.
			expectRefused( ql, UserPK{}, EHttpStatus::Unauthorized );
			expectRefused( ql, _intruder, EHttpStatus::Forbidden );
		}
		EXPECT_TRUE( QL().QuerySync(select, {}, root).empty() );
		let id = GetId( QL().QuerySync(create, {}, admin) );
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation purgeProvider( id:{} )", id), {}, admin) );
		EXPECT_TRUE( QL().QuerySync(select, {}, root).empty() );

		PurgeAcl( admin, grant, system );
		if( !wasEnforced )
			Delete( "resources", usersPK, system );
		PurgeUser( admin, root );
	}

	//The controls:  the system still writes them - the seeds, ResourceLoadAwait and the hub's provider insert all run as System -
	//and any known user reads outside a table's ops:  the rights enum every permission table renders.
	TEST_F( UndeclaredWriteTests, TheSystemWritesAndAKnownUserReads ){
		const UserPK system{ UserPK::System };
		let id = GetId( QL().QuerySync(Ƒ("createProvider( slug:\"ghsa-g354-control\", providerType:{} ){{ id }}", underlying(EProviderType::Key)), {}, system) );
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation purgeProvider( id:{} )", id), {}, system) );
		EXPECT_FALSE( QL().QuerySync<jarray>("rights{ id name }", {}, _intruder).empty() );
	}

	//No resource row can enforce a read outside a table's ops, so it needs a known user:  anonymous, the rights tables returned
	//every login's email and the whole grant table even with users and acl enforced (review #8).
	TEST_F( UndeclaredWriteTests, ReadsOutsideTheOpsNeedAKnownUser ){
		let root = GetRoot();
		const vector<string> reads{ "identities{ id name email }", "permissionRights{ id allowed resource{ slug } }", "rights{ id name }", "resources{ id }" };
		const UserPK deleted{ GetId(GetUser("ghsa-g354-deleted", root)) };
		Delete( "users", deleted.Value, root );
		for( let& ql : reads ){
			expectRefused( ql, UserPK{}, EHttpStatus::Unauthorized );
			expectRefused( ql, deleted, EHttpStatus::Forbidden );
			EXPECT_NO_THROW( QL().QuerySync<jarray>(ql, {}, _intruder) ) << ql;
		}
		PurgeUser( deleted, root );
	}

	//__type{ enumValues } read any column of any table unauthorized - every login's email to an anonymous caller (review #15).  A
	//lookup table (IsEnum) still answers anyone;  any other table's rows are a read, authorized as one, id and name alone.
	TEST_F( UndeclaredWriteTests, EnumValuesOfATableAreARead ){
		const string identity{ "__type( name:\"Identity\" ){ enumValues{ id name email } }" };
		expectRefused( identity, UserPK{}, EHttpStatus::Unauthorized );
		let values = Json::AsArray( QL().QuerySync(identity, {}, _intruder), "enumValues" );
		ASSERT_FALSE( values.empty() );
		EXPECT_FALSE( Json::AsObject(values[0]).contains("email") ) << "id and name only";
		EXPECT_FALSE( Json::AsArray(QL().QuerySync("__type( name:\"Provider\" ){ enumValues{ id name } }", {}, UserPK{}), "enumValues").empty() ) << "the login page's, signed out";
		EXPECT_FALSE( Json::AsArray(QL().QuerySync("__type( name:\"ProviderType\" ){ enumValues{ id name } }", {}, UserPK{}), "enumValues").empty() );
	}

	//PurgeAwait::Statements authorized every table of the extension chain, so a user purge also asked Purge on identities -
	//which declares no ops, and the gate would have refused every executer but the system.  The extension's own Purge covers the
	//row it extends.
	TEST_F( UndeclaredWriteTests, AnExtensionsPurgeStillCoversTheRowItExtends ){
		let root = GetRoot();
		const UserPK user{ GetId(GetUser("ghsa-g354-purged", root)) };
		EXPECT_NO_THROW( PurgeUser(user, root) );
		EXPECT_TRUE( SelectUser("ghsa-g354-purged", root, nullopt, true).empty() );
	}
}

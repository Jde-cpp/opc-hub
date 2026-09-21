#include "gtest/gtest.h"
#include <jde/access/server/awaits/RoleAwait.h>
#include <jde/access/Authorize.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/settings.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include "../src/awaits/RoleLoadAwait.h"
#include "globals.h"

#define let const auto
namespace Jde::Access::Tests{
	using namespace Json;
	class RoleTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()->void;
	};

	α RoleTests::SetUpTestCase()->void{
	}
	//AclTests.cpp
	α CreateAcl( IdentityPK identityPK, ERights allowed, ERights denied, string resource, UserPK executer )ε->PermissionRightsPK;
	α SelectAcl( IdentityPK identityPK, string resourceSlug )ε->jobject;
	α PurgeAcl( IdentityPK identityPK, PermissionRightsPK permissionPK, UserPK executer )ε->void;
	α CreateAcl( IdentityPK identityPK, RolePK rolePK, UserPK executer )ε->void;
	α SelectAcl( IdentityPK identityPK, RolePK rolePK )ε->jobject;
	α restoreResource( string name, UserPK executer )ε->void;
	α RemoveRolePermission( RolePK rolePK, PermissionPK permissionPK, UserPK userPK )ε->jvalue{
		let remove = Ƒ( "mutation removeRole( id:{}, permissionRight:{{id:{}}} )", rolePK, permissionPK );
		return BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(remove, {}, Schemas()), userPK} );
	}
	α RemoveRoleMember( RolePK parentRolePK, RolePK childRolePK, UserPK userPK )ε->jvalue{
		jobject vars{ {"parent", parentRolePK}, {"child", childRolePK} };
		let q = "mutation removeRole( id:$parent, role:{id:$child} )";
		return BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(q, vars, Schemas()), userPK} );
	}
	α GetRolePermission( RolePK rolePK, sv resourceName, UserPK executer )ε->jobject{
		jobject vars{ {"roleId", rolePK}, {"resource", resourceName} };
		let q = "role( id:$roleId ){permissionRight{id allowed denied resource(slug:$resource,criteria:null)} }";
		let role = BlockTAwait<jvalue>( Server::RoleAwait{QL::ParseQuery(q, vars, Schemas()), executer} ).as_object(); //{"role":{"member":{"id":1,"allowed":[],"denied":[]}}}
		return Json::FindDefaultObjectPath( role, "permissionRight" );
	}
	α GetRoleChild( RolePK parentRolePK, RolePK childRolePK, UserPK userPK )ε->jobject{
		jobject vars = { {"parent", parentRolePK}, {"child",childRolePK} };
		let q = "role( id:$parent ){role(id:$child){id slug deleted} }";
		auto y = BlockTAwait<jvalue>( Server::RoleAwait{QL::ParseQuery(q, vars, Schemas()), userPK} );
		return y.is_object() ? Json::FindDefaultObjectPath( y.get_object(), "role" ) : jobject{};
	}

	α AddRolePermission( RolePK rolePK, sv resourceName, ERights allowed, ERights denied, UserPK userPK )ε->jobject{
		auto permission = GetRolePermission( rolePK, resourceName, userPK );
		if( !permission.empty() ){
			let existingAllowed = ToRights( Json::AsArray(permission, "allowed") );
			let existingDenied = ToRights( Json::AsArray(permission, "denied") );
			if( allowed!=existingAllowed || denied!=existingDenied ){
				let update = Ƒ( "mutation updatePermissionRight( id:{}, allowed:{}, denied:{} )", GetId(permission), underlying(allowed), underlying(denied) );
				QL().QuerySync<jvalue>( update, {}, userPK );
				permission = GetRolePermission( rolePK, resourceName, userPK );
			}
		}
		else{
			jobject vars{ {"roleId", rolePK}, {"allowed", underlying(allowed)}, {"denied", underlying(denied)}, {"resource", resourceName} };
			auto q = "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:$denied, resource:{slug:$resource}} )";
			BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(q, vars, Schemas()), userPK} );
			permission = GetRolePermission( rolePK, resourceName, userPK );
		}
		return permission;
	}
	α InsertRoleMember( RolePK parentRolePK, RolePK childRolePK, UserPK userPK )ε->jvalue{ //the bare mutation - AddRoleMember's pre-read trips the roles read gate before the mutation's own gate is reached.
		jobject vars{ {"parent", parentRolePK}, {"child", childRolePK} };
		let q = "mutation addRole( id:$parent, role:{id:$child} )";
		return BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(q, vars, Schemas()), userPK} );
	}
	α AddRoleMember( RolePK parentRolePK, RolePK childRolePK, UserPK userPK )ε->jobject{
		auto y = GetRoleChild( parentRolePK, childRolePK, userPK );
		if( y.empty() ){
			InsertRoleMember( parentRolePK, childRolePK, userPK );
			y = GetRoleChild( parentRolePK, childRolePK, userPK );
		}
		return y;
	}

	TEST_F( RoleTests, Crud ){
		let pk = TestCrud( "role", "roleTest", GetRoot() );
		TestPurge( "role", pk, GetRoot() );
	}

	Ω getRole( str slug, UserPK executer )ε->jobject{ return Get("role", slug, executer); }

	TEST_F( RoleTests, AddRemove ){
		let rolePK = GetId( getRole("rolePermissionsTest", GetRoot()) );
		auto initial = AddRolePermission( rolePK, "users", ERights::All, ERights::None, GetRoot() );
		ASSERT_EQ( ToRights( Json::AsArrayPath(initial, "allowed") ), ERights::All );
		ASSERT_EQ( ToRights( Json::AsArrayPath(initial, "denied") ), ERights::None );

		RemoveRolePermission( rolePK, GetId(initial), GetRoot() );
		auto roleMember = GetRolePermission( rolePK, "users", GetRoot() );
		ASSERT_TRUE( roleMember.empty() );

		AddRolePermission( rolePK, "users", ERights::Read, ERights::Update, GetRoot() );//purge with permissions.
		Purge( "role", rolePK, GetRoot() );
	}

	TEST_F( RoleTests, Recursion ){
		const RolePK aRole{ GetId(getRole("roleRecursionA", GetRoot())) };
		const RolePK bRole{ GetId(getRole("roleRecursionB", GetRoot())) };
		AddRoleMember( aRole, bRole, GetRoot() );
		const RolePK cRole{ GetId(getRole("roleRecursionC", GetRoot())) };
		AddRoleMember( bRole, cRole, GetRoot() );

		const RolePK dRole{ GetId(getRole("roleRecursionD", GetRoot())) };
		EXPECT_THROW( AddRoleMember( dRole, dRole, GetRoot() ), Exception );
		AddRoleMember( cRole, dRole, GetRoot() );
		EXPECT_THROW( AddRoleMember( dRole, aRole, GetRoot() ), Exception );
		//TODO test implement deleted roles.
	}

	TEST_F( RoleTests, RemoveChild ){
		const RolePK parent{ GetId(getRole("roleRemoveChildParent", GetRoot())) };
		const RolePK child{ GetId(getRole("roleRemoveChildChild", GetRoot())) };
		ASSERT_FALSE( AddRoleMember(parent, child, GetRoot()).empty() );
		RemoveRoleMember( parent, child, GetRoot() );
		ASSERT_TRUE( GetRoleChild(parent, child, GetRoot()).empty() );//the membership is gone...
		ASSERT_FALSE( getRole("roleRemoveChildChild", GetRoot()).empty() );//...but the child role itself survives.
		Purge( "role", parent, GetRoot() );
		Purge( "role", child, GetRoot() );
	}

	//access-review3 #2: access_role_remove's deletes were keyed on the permission alone, and both ids come from the client -
	//a (role, permission) pair that doesn't match destroyed another role's rights row, or a direct acl grant's.  The proc
	//now refuses a permission the role doesn't own.  Under ctest (sqlite) the old proc was already masked by the
	//native-proc transaction's rollback on the trailing fk failure; mysql/sqlServer autocommit each statement.
	TEST_F( RoleTests, RemoveScopedToRole ){
		const RolePK aRole{ GetId(getRole("roleRemoveScopeA", GetRoot())) };
		const RolePK bRole{ GetId(getRole("roleRemoveScopeB", GetRoot())) };
		AddRolePermission( aRole, "users", ERights::Read, ERights::None, GetRoot() );
		let bPermission = AddRolePermission( bRole, "users", ERights::Read, ERights::Update, GetRoot() );
		EXPECT_THROW( RemoveRolePermission(aRole, GetId(bPermission), GetRoot()), Exception );//B's permission through A.
		let bAfter = GetRolePermission( bRole, "users", GetRoot() );
		ASSERT_FALSE( bAfter.empty() );
		EXPECT_EQ( GetId(bAfter), GetId(bPermission) );
		EXPECT_EQ( ToRights(Json::AsArrayPath(bAfter, "allowed")), ERights::Read );
		EXPECT_EQ( ToRights(Json::AsArrayPath(bAfter, "denied")), ERights::Update );

		const UserPK victim{ GetId(GetUser("roleRemoveScopeVictim", GetRoot())) };
		let aclPermissionPK = CreateAcl( victim, ERights::Read, ERights::None, "users", GetRoot() );//a direct grant, in no role at all.
		EXPECT_THROW( RemoveRolePermission(aRole, aclPermissionPK, GetRoot()), Exception );
		EXPECT_EQ( GetId(SelectAcl(victim, "users")), aclPermissionPK );
		PurgeAcl( victim, aclPermissionPK, GetRoot() );
		Purge( "role", aRole, GetRoot() );
		Purge( "role", bRole, GetRoot() );
	}

	//access-review3 #7:  Add()/Remove() are noexcept (Suspend is) and asserted the arg's shape with Json::AsObject, so a non-object
	//`role`/`permissionRight` from a client threw across the boundary into std::terminate - the whole AppServer, for one mutation.
	//Refused with an exception now.
	TEST_F( RoleTests, MalformedArgsAreRefusedNotFatal ){
		auto mutate = []( sv ql ){ return BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(string{ql}, {}, Schemas()), GetRoot()} ); };
		EXPECT_THROW( mutate("mutation addRole( id:1, role:1 )"), Exception );
		EXPECT_THROW( mutate("mutation addRole( id:1, role:null )"), Exception );
		EXPECT_THROW( mutate("mutation addRole( id:1, permissionRight:5 )"), Exception );
		EXPECT_THROW( mutate("mutation removeRole( id:1, role:\"x\" )"), Exception );
		EXPECT_THROW( mutate("mutation removeRole( id:1, permissionRight:5 )"), Exception ); //RemovePermission reads the id inside its try.
		EXPECT_THROW( mutate("mutation addRole( id:1 )"), Exception ); //neither key - the refusal that was already there.
	}

	//access-review3 #13:  access_role_purge never cleared the acl rows granting the role, nor the role's own membership in a parent,
	//so its final delete of the role's access_permissions row failed on their fk - a Conflict under sqlite's rollback, a role left
	//half-destroyed on the autocommit dialects.  Neither shape was covered:  the other purges here are of roles never granted.
	TEST_F( RoleTests, PurgeAssignedRole ){
		let root = GetRoot();
		restoreResource( "groups", root );
		const RolePK rolePK{ (RolePK)GetId(getRole("rolePurgeAssigned", root)) };
		AddRolePermission( rolePK, "groups", ERights::Read, ERights::None, root );
		const UserPK user{ GetId(GetUser("rolePurgeAssignedUser", root)) };
		CreateAcl( user, rolePK, root );
		ASSERT_FALSE( SelectAcl(user, rolePK).empty() );
		ASSERT_EQ( Authorizer()->Rights("access", "groups", user), ERights::Read );

		EXPECT_NO_THROW( Purge("role", rolePK, root) ); //used to throw on the acl fk.
		EXPECT_TRUE( QL().QuerySync<jarray>(R"(roles( slug:"rolePurgeAssigned" ){ id })", {}, root).empty() );
		EXPECT_EQ( QL().QuerySync<jarray>(Ƒ("acl( identityId:{} ){{ identityId permissionRight{{ id }} }}", user.Value), {}, root).size(), 0u ) << "the grant went with the role";
		EXPECT_EQ( Authorizer()->Rights("access", "groups", user), ERights::None ); //and the cache dropped it.
	}
	TEST_F( RoleTests, PurgeNestedChildRole ){
		let root = GetRoot();
		const RolePK parent{ (RolePK)GetId(getRole("rolePurgeNestedParent", root)) };
		const RolePK child{ (RolePK)GetId(getRole("rolePurgeNestedChild", root)) };
		ASSERT_FALSE( AddRoleMember(parent, child, root).empty() );

		EXPECT_NO_THROW( Purge("role", child, root) ); //used to throw on the parent's membership fk.
		EXPECT_TRUE( GetRoleChild(parent, child, root).empty() ) << "no membership row left pointing at a permission that is no longer a role";
		EXPECT_FALSE( QL().QuerySync<jarray>(R"(roles( slug:"rolePurgeNestedParent" ){ id })", {}, root).empty() ) << "the parent survives";
		Purge( "role", parent, root );
	}
	//Purging a role stripped every direct grant of its child roles:  access_role_purge deleted the acl rows of every member of the
	//role, and a member that is itself a role is shared - its grants are its own, not the parent's.  The cache never dropped them
	//(Authorize::PurgeRole sweeps the references to the purged role only), so the loss surfaced at the next restart.  The
	//parent's own permission rows still go with it - a purge leaves no orphans behind.
	TEST_F( RoleTests, PurgeParentKeepsChildGrants ){
		let root = GetRoot();
		restoreResource( "groups", root );
		const RolePK parent{ (RolePK)GetId(getRole("rolePurgeParentGrants", root)) };
		const RolePK child{ (RolePK)GetId(getRole("rolePurgeChildGrants", root)) };
		ASSERT_FALSE( AddRoleMember(parent, child, root).empty() );
		let parentPermission = GetId( AddRolePermission(parent, "groups", ERights::Update, ERights::None, root) );
		let childPermission = GetId( AddRolePermission(child, "groups", ERights::Read, ERights::None, root) );
		const UserPK user{ GetId(GetUser("rolePurgeChildGrantsUser", root)) };
		CreateAcl( user, parent, root );
		CreateAcl( user, child, root ); //the child is granted directly as well as through its parent.
		ASSERT_EQ( Authorizer()->Rights("access", "groups", user), ERights::Read|ERights::Update );
		let permissionRow = [&]( uint id ){ return QL().QuerySync<jarray>( Ƒ("permissionRights( id:{} ){{ id }}", id), {}, root ); };
		ASSERT_EQ( permissionRow(parentPermission).size(), 1u );

		EXPECT_NO_THROW( Purge("role", parent, root) );
		EXPECT_TRUE( SelectAcl(user, parent).empty() ) << "the parent's own grant went with it";
		EXPECT_FALSE( SelectAcl(user, child).empty() ) << "the child's direct grant survives its parent";
		EXPECT_FALSE( GetRolePermission(child, "groups", root).empty() ) << "and so do the child's rights";
		EXPECT_EQ( permissionRow(childPermission).size(), 1u );
		EXPECT_TRUE( permissionRow(parentPermission).empty() ) << "the parent's own permission row is gone - no orphan";
		EXPECT_EQ( Authorizer()->Rights("access", "groups", user), ERights::Read ) << "the cache agrees with the db";

		Purge( "role", child, root );
		EXPECT_TRUE( SelectAcl(user, child).empty() );
		EXPECT_TRUE( permissionRow(childPermission).empty() );
	}

	//The seed's spelling - libs/access/config/release.roles, applied by DB::SyncData(".roles") through LocalQL::Upsert once the
	//access server is up:  createRole by slug, addRole naming the role and its member roles by slug (a file cannot know the
	//pks it just created), permissions by schema+slug.  Upsert skips a createRole whose slug exists and always runs an
	//addRole - so a second pass over the same text (every -sync start) changes nothing and fails nothing.
	TEST_F( RoleTests, SeedBySlug ){
		let root = GetRoot();
		for( let slug : {"seedAdmin", "seedViewer"} ){//a previous run's rows would make the first pass a rerun.
			for( let& v : QL().QuerySync<jarray>(Ƒ(R"(roles( slug:"{}" ){{ id }})", slug), {}, root) )
				Purge( "role", GetId(Json::AsObject(v)), root );
		}
		constexpr sv seed = R"(mutation{
			createRole( slug:"seedViewer", name:"Seed Viewer", description:"seed" )
			addRole( slug:"seedViewer", permissionRight:{ allowed:2, denied:0, resource:{ schemaName:"access", slug:"groups" } } )
			createRole( slug:"seedAdmin", name:"Seed Admin", description:"seed" )
			addRole( slug:"seedAdmin", role:{ slug:"seedViewer" } )
			addRole( slug:"seedAdmin", permissionRight:{ allowed:32, denied:0, resource:{ schemaName:"access", slug:"groups" } } )
		})";
		const UserPK system{ UserPK::System };
		for( uint pass=0; pass<2; ++pass ){
			ASSERT_NO_THROW( QL().Upsert(string{seed}, {}, system) ) << "pass " << pass;
			const RolePK viewer{ (RolePK)GetId(getRole("seedViewer", root)) }, admin{ (RolePK)GetId(getRole("seedAdmin", root)) };
			EXPECT_EQ( ToRights(Json::AsArray(GetRolePermission(viewer, "groups", root), "allowed")), ERights::Read ) << "pass " << pass;
			EXPECT_EQ( ToRights(Json::AsArray(GetRolePermission(admin, "groups", root), "allowed")), ERights::Administer ) << "pass " << pass;
			EXPECT_FALSE( GetRoleChild(admin, viewer, root).empty() ) << "pass " << pass;//and the second pass did not insert the membership twice - a duplicate key would have thrown above.
		}
		const RolePK admin{ (RolePK)GetId(getRole("seedAdmin", root)) };
		Purge( "role", admin, root );
		Purge( "role", (RolePK)GetId(getRole("seedViewer", root)), root );
	}

	//The shipped seed itself - libs/access/config/release.roles, installed as access.roles (setup/OpcHubSetup.nsi) - applied the way
	//the hub applies it, twice:  the six roles, Owner over System Administrator over Viewer, Viewer reading and the administrator
	//administering every table, Owner holding what neither may.  The resources it grants on that no server has registered here
	//(opc.install) are created by access_role_add as the mutation names them.
	TEST_F( RoleTests, ReleaseRolesSeed ){
		let root = GetRoot();
		let& scriptPaths = Settings::FindDefaultArray( "/dbServers/scriptPaths" ); //<repo>/libs/access/config/sql/<dialect>
		ASSERT_FALSE( scriptPaths.empty() );
		let seed = IO::Load( fs::path{string{Json::AsSV(scriptPaths[0])}}.parent_path().parent_path()/"release.roles" );
		const UserPK system{ UserPK::System };
		for( uint pass=0; pass<2; ++pass )
			ASSERT_NO_THROW( QL().Upsert(seed, {}, system) ) << "pass " << pass;
		const RolePK viewer{ (RolePK)GetId(getRole("viewer", root)) }, sa{ (RolePK)GetId(getRole("sa", root)) }, owner{ (RolePK)GetId(getRole("owner", root)) };
		EXPECT_FALSE( GetRoleChild(sa, viewer, root).empty() );
		EXPECT_FALSE( GetRoleChild(owner, sa, root).empty() );
		EXPECT_FALSE( GetRoleChild(owner, viewer, root).empty() );
		for( let slug : {"engineer", "operator", "maint-tech"} )
			EXPECT_FALSE( GetRoleChild((RolePK)GetId(getRole(slug, root)), viewer, root).empty() ) << slug;
		EXPECT_EQ( ToRights(Json::AsArray(GetRolePermission(viewer, "users", root), "allowed")), ERights::Read );
		EXPECT_EQ( ToRights(Json::AsArray(GetRolePermission(sa, "users", root), "allowed")), ERights::Administer );
		EXPECT_EQ( ToRights(Json::AsArray(GetRolePermission(owner, "users", root), "allowed")), ERights::Create|ERights::Update|ERights::Delete|ERights::Purge|ERights::Subscribe|ERights::Execute );
		EXPECT_EQ( ToRights(Json::AsArray(GetRolePermission(owner, "resources", root), "allowed")), ERights::Delete|ERights::Subscribe );
		//reviews/m2-closing.md #8:  every resource the gateway gates on, not just the one with a table behind it - or enforcing
		//gateway/sessions or gateway/search locks out every role this seed ships, the administrators included.
		for( let slug : {"serverConnections", "sessions", "search"} )
			EXPECT_EQ( ToRights(Json::AsArray(GetRolePermission(viewer, slug, root), "allowed")), ERights::Read ) << slug;
		//reviews/m2-closing.md #9 - the role->grant table install-issues #34's fix shape asked for:  what each of the three job
		//roles grants *itself*, beside what its description says it is for.  Engineer's said "Configures server connections"
		//over nothing but Viewer's Read - the defect PR #174 reworded out of Operator and Maintenance Technician and left
		//standing here.  It carries that right now, and that one only;  the other two are Viewer by another name, carry
		//nothing of their own, and say so.  The provider row a new connection brings is written under the gateway's own
		//identity (ProviderMAwait), so gateway/serverConnections is all that configuring one asks of its user.
		constexpr array gated{ "acl", "groups", "providerTypes", "roles", "users", "instances", "instanceTagLevels", "serverConnections", "sessions", "search", "nodeIds" };
		let direct = [&]( str role, sv resource ){ let p = GetRolePermission( (RolePK)GetId(getRole(role, root)), resource, root ); return p.empty() ? ERights::None : ToRights( Json::AsArray(p, "allowed") ); };
		let description = [&]( sv role )->string{//from the file - what an operator reads on the Roles page.
			let start = seed.find( Ƒ("createRole( slug:\"{}\"", role) );
			return start==string::npos ? string{} : seed.substr( start, seed.find('\n', start)-start );
		};
		for( let resource : gated )
			EXPECT_EQ( direct("engineer", resource), sv{resource}=="serverConnections" ? ERights::Create|ERights::Update|ERights::Delete : ERights::None ) << "engineer on " << resource;
		EXPECT_TRUE( description("engineer").contains("server connections") ) << description( "engineer" );
		for( let role : {"operator", "maint-tech"} ){
			for( let resource : gated )
				EXPECT_EQ( direct(role, resource), ERights::None ) << role << " on " << resource;
			EXPECT_TRUE( description(role).contains("No configuration") ) << role << " grants nothing of its own, and its description has to say so: " << description( role );
		}
		for( let slug : {"owner", "engineer", "operator", "maint-tech", "sa", "viewer"} )//parents before children - a purge strips the parent's memberships, the children survive.
			Purge( "role", (RolePK)GetId(getRole(slug, root)), root );
	}

	//access-review3 #15:  sqlServer/access_role_add.sql inserted the resource name as sent, where mysql and sqlite coalesce it over
	//the slug - and the live grant-a-role-on-this-node path sends no name, into a not-null column.  Source parity, since no
	//sqlServer runs here:  every dialect's insert into access_resources has to coalesce the name parameter.
	TEST_F( RoleTests, AccessRoleAddCoalescesResourceNameInEveryDialect ){
		let& scriptPaths = Settings::FindDefaultArray( "/dbServers/scriptPaths" ); //<repo>/libs/access/config/sql/<dialect>
		ASSERT_FALSE( scriptPaths.empty() );
		let sqlRoot = fs::path{ string{Json::AsSV(scriptPaths[0])} }.parent_path();
		for( let& [file, spelling] : std::initializer_list<std::pair<sv,sv>>{ {"mysql/access_role_add.sql", "coalesce(_resourceName, _resourceSlug)"}, {"sqlServer/access_role_add.sql", "coalesce(@resourceName, @resourceSlug)"}, {"sqlite/access_role_add.cpp", "coalesce(?, ?)"} } ){
			let text = IO::Load( sqlRoot/file );
			let insert = text.find( "insert into access_resources(" );
			ASSERT_NE( insert, string::npos ) << file;
			let line = text.substr( insert, text.find('\n', insert)-insert );
			EXPECT_NE( line.find(spelling), string::npos ) << file << ": " << line;
		}
	}
	//access-review3 #27:  the server-dialect procs declared allowed/denied narrower than the ulong columns they write (tinyint on
	//sqlServer, and mysql's upsert), and slug/criteria wider than theirs - a right above bit 7 would have failed on one dialect
	//and not the others, and an oversized slug was refused at the insert rather than the proc boundary.  Source parity, driven
	//by the meta:  every parameter that carries a column is declared at that column's width.
	TEST_F( RoleTests, ProcParametersMatchTheColumns ){
		let& rights = *GetTable( "permission_rights" );
		ASSERT_EQ( rights.GetColumnPtr("allowed")->Type, DB::EType::ULong ); //64 bits - what "bigint" below stands for.
		let& resources = *GetTable( "resources" );
		let width = [&]( sv column ){ return resources.GetColumnPtr(string{column})->MaxLength.value_or(0); };
		let& scriptPaths = Settings::FindDefaultArray( "/dbServers/scriptPaths" );
		ASSERT_FALSE( scriptPaths.empty() );
		let sqlRoot = fs::path{ string{Json::AsSV(scriptPaths[0])} }.parent_path();
		const vector<std::pair<string,vector<string>>> expectations{
			{ "mysql/access_role_add.sql", {"_allowed bigint unsigned", "_denied bigint unsigned", Ƒ("_resourceSlug varchar({})", width("slug")), Ƒ("_schema varchar({})", width("schema_name")), Ƒ("_criteria varchar({})", width("criteria"))} },
			{ "sqlServer/access_role_add.sql", {"@allowed bigint", "@denied bigint", Ƒ("@resourceSlug varchar({})", width("slug")), Ƒ("@schema varchar({})", width("schema_name")), Ƒ("@criteria varchar({})", width("criteria"))} },
			{ "mysql/access_ac_upsert_permission.sql", {"_allowed bigint unsigned", "_denied bigint unsigned"} },
			{ "sqlServer/access_ac_upsert_permission.sql", {"@allowed bigint", "@denied bigint"} }
		};
		for( let& [file, fragments] : expectations ){
			let text = IO::Load( sqlRoot/file );
			let start = text.find( "create" ); //"create procedure"/"create or alter proc" - the parameter list follows on that line (mysql's file opens with a drop).
			ASSERT_NE( start, string::npos ) << file;
			let header = text.substr( start, text.find('\n', start)-start );
			for( let& fragment : fragments )
				EXPECT_NE( header.find(fragment), string::npos ) << file << " lacks '" << fragment << "': " << header;
		}
	}
	//and the behaviour itself, on the dialects this suite runs against:  a role permission on a resource nothing has registered
	//yet, with no name - the resource is created, named after its slug.
	TEST_F( RoleTests, AddPermissionOnNewResourceWithoutName ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		constexpr sv slug{ "parityNew" }, criteria{ "x" };
		//criteria-scoped, so still created active (install-issues #25 ships only a criteria-null root resource unenforced).
		let select = Ƒ( R"(resources( schemaName:"access", slug:"{}", criteria:"{}" ){{ id name }})", slug, criteria );
		for( let& v : QL().QuerySync<jarray>(select, {}, root) ) //a previous run's row would make this a no-op.
			Purge( "resource", GetId(Json::AsObject(v)), root );
		const RolePK rolePK{ (RolePK)GetId(getRole("roleParityNew", root)) };
		let q = Ƒ( R"(addRole( id:{}, permissionRight:{{ allowed:2, denied:0, resource:{{ schemaName:"access", slug:"{}", criteria:"{}" }} }} ))", rolePK, slug, criteria );
		let added = BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(q, {}, Schemas()), root} ).as_object();
		let resources = QL().QuerySync<jarray>( select, {}, root );
		ASSERT_EQ( resources.size(), 1u );
		EXPECT_EQ( Json::AsSV(Json::AsObject(resources[0]), "name"), slug ) << "name coalesced over the slug";
		RemoveRolePermission( rolePK, Json::AsNumber<PermissionPK>(added, "permissionRight/id"), system ); //root holds nothing over the new resource.
		Purge( "resource", GetId(Json::AsObject(resources[0])), root );
		Purge( "role", rolePK, root );
	}

	//install-issues #25: a root (criteria-null) resource that exists only because a role referenced it ships unenforced - created
	//deleted - so the OpcServer's own `opc.install nodeIds`, created this way by the hub's role seed before the OpcServer connects
	//and declares it, no longer ships enforced and closes node access on a fresh install.  The `deleted` column in the selection
	//is what returns the unenforced row; the criteria-scoped case above stays enforced.
	TEST_F( RoleTests, AddPermissionOnNewRootResourceShipsUnenforced ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		constexpr sv slug{ "parityRoot" };
		let select = Ƒ( R"(resources( schemaName:"access", slug:"{}", criteria:null ){{ id deleted }})", slug );
		for( let& v : QL().QuerySync<jarray>(select, {}, root) )
			Purge( "resource", GetId(Json::AsObject(v)), root );
		const RolePK rolePK{ (RolePK)GetId(getRole("roleParityRoot", root)) };
		let q = Ƒ( R"(addRole( id:{}, permissionRight:{{ allowed:2, denied:0, resource:{{ schemaName:"access", slug:"{}" }} }} ))", rolePK, slug );
		let added = BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(q, {}, Schemas()), root} ).as_object();
		let resources = QL().QuerySync<jarray>( select, {}, root );
		ASSERT_EQ( resources.size(), 1u );
		EXPECT_FALSE( Json::AsObject(resources[0]).at("deleted").is_null() ) << "a role-referenced root resource should ship unenforced";
		RemoveRolePermission( rolePK, Json::AsNumber<PermissionPK>(added, "permissionRight/id"), system );
		Purge( "resource", GetId(Json::AsObject(resources[0])), root );
		Purge( "role", rolePK, root );
	}

	TEST_F( RoleTests, DeletedLoad ){
		const RolePK rolePK{ GetId(getRole("roleDeletedLoadTest", GetRoot())) };
		Delete( "role", rolePK, GetRoot() );
		let roles = BlockAwait<RoleLoadAwait, flat_map<RolePK,Role>>( RoleLoadAwait{QLPtr(), {UserPK::System}} );
		auto p = roles.find( rolePK );
		ASSERT_NE( p, roles.end() );
		EXPECT_TRUE( p->second.IsDeleted );
		Purge( "role", rolePK, GetRoot() );
	}
}
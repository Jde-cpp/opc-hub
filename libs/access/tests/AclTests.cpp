#include "gtest/gtest.h"
#include <jde/fwk/io/json.h>
#include <jde/fwk/str.h>
#include <jde/access/server/awaits/AclAwait.h>
#include <jde/access/server/awaits/UserRightsAwait.h>
#include <jde/access/Authorize.h>
#include <jde/access/AccessListener.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include "../src/accessInternal.h"
#include "../src/awaits/AclLoadAwait.h"
#include "globals.h"

#define let const auto
namespace Jde::Access::Tests{
	α GetRolePermission( RolePK rolePK, sv resourceName, UserPK executer )ε->jobject;
	α AddRolePermission( RolePK rolePK, sv resourceName, ERights allowed, ERights denied, UserPK executer )ε->jobject;
	α AddRoleMember( RolePK parentRolePK, RolePK childRolePK, UserPK executer )ε->jobject;
	α RemoveRolePermission( RolePK rolePK, PermissionPK permissionPK, UserPK executer )ε->jvalue;
	α RemoveRoleMember( RolePK parentRolePK, RolePK childRolePK, UserPK executer )ε->jvalue;
	α InsertRoleMember( RolePK parentRolePK, RolePK childRolePK, UserPK executer )ε->jvalue;
	α GetRoleChild( RolePK parentRolePK, RolePK childRolePK, UserPK executer )ε->jobject;
	using namespace Json;
	class AclTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()->void;

		α TestEnabeledPermissions( str resourceName, str slug, UserPK executer )ε;
		static flat_map<string,jobject> _users;
		static flat_map<string,UserPK> _usersPKs;
		static ResourcePK _resourcePK;
	};
	flat_map<string,jobject> AclTests::_users;
	flat_map<string,UserPK> AclTests::_usersPKs;
	ResourcePK AclTests::_resourcePK;

	α SelectAcl( IdentityPK identityPK, string resourceSlug )ε->jobject{
		jobject vars{ {"identityId", identityPK.Underlying()}, {"resource", resourceSlug} };
		let q = "acl( identityId:$identityId ){ identityId permissionRight{ id allowed denied resource(slug:$resource){deleted}} }";
		let acl = BlockTAwait<jvalue>( Server::AclQLSelectAwait{ QL::ParseQuery(q, vars, Schemas()), GetRoot()} ).as_array();
		return acl.empty() ? jobject{} : Json::AsObject(acl[0], "/permissionRight");
	}
	α SelectAcl( IdentityPK identityPK, RolePK rolePK )ε->jobject{
		let ql = Ƒ( "acl(identityId:{})<identityId role(id:{})<id slug deleted>>", identityPK.Underlying(), rolePK.Value );
		let acl = QL().QuerySync<jarray>( Str::Replace(Str::Replace(ql,"<","{"), ">", "}"), {}, GetRoot() );
		return acl.empty() ? jobject{} : Json::AsObject(acl[0], "/role");
	}
	α CreateAcl( IdentityPK identityPK, ERights allowed, ERights denied, string resource, UserPK executer )ε->PermissionPK{
		let resourcePK = AsNumber<ResourcePK::Type>( SelectResource(resource, {UserPK::System}, true), "id" );
		jobject vars{ {"id", identityPK.Underlying()}, {"allowed", underlying(allowed)}, {"denied", underlying(denied)}, {"resource", resourcePK} };
		let q = "createAcl( identity:{ id:$id }, permissionRight:{ allowed:$allowed, denied:$denied, resource:{id:$resource}} ){ permissionRight{id} }";
		let y = BlockTAwait<jvalue>( Server::AclQLAwait{ QL::ParseM(q, vars, Schemas()), executer} ).as_object();
		return PermissionPK{Json::AsNumber<PermissionPK::Type>( y, "permissionRight/id" )};
	}
	α PurgeAcl( IdentityPK identityPK, PermissionPK permissionPK, UserPK executer )ε->void{
		let q = Ƒ( "purgeAcl( identity:{{ id:{} }}, permissionRight:{{ id:{} }} )", identityPK.Underlying(), permissionPK.Value );
		BlockTAwait<jvalue>( Server::AclQLAwait{ QL::ParseM(q, {}, Schemas()), executer} );
	}
	α CreateAcl( IdentityPK identityPK, RolePK rolePK, UserPK executer )ε->void{
		let existing = SelectAcl( identityPK, rolePK );
		if( existing.empty() ){
			jobject vars{ {"id", identityPK.Underlying()}, {"roleId", rolePK.Value} };
			let q = "createAcl( identity:{ id:$id }, role:{ id:$roleId } )";
			BlockTAwait<jvalue>( Server::AclQLAwait{ QL::ParseM(q, vars, Schemas()), executer} );
		}
	}

	α GetAcl( IdentityPK identityPK, string resource, ERights allowed, ERights denied )ε->jobject{
		auto entry = SelectAcl( identityPK, resource );
		if( entry.empty() ){
			CreateAcl( identityPK, allowed, denied, resource, GetRoot() );
			entry = SelectAcl( identityPK, resource );
		}
		else{
			let existingAllowed = (ERights)Json::AsNumber<uint8>( entry, "allowed" ); //ToRights( Json::AsArray(entry, "allowed") );
			let existingDenied = (ERights)Json::AsNumber<uint8>( entry, "denied" ); //ToRights( Json::AsArray(entry, "denied") );
			if( allowed!=existingAllowed || existingDenied!=denied ){
				let update = Ƒ( "mutation updatePermissionRight( id:{}, allowed:{}, denied:{} )", Json::AsNumber<PermissionPK::Type>(entry, "id"), underlying(allowed), underlying(denied) );
				let updateJson = QL().QuerySync( update, {}, GetRoot() );
				entry = SelectAcl( identityPK, resource );
			}
		}
		return entry;
	}
	α RestoreResource( string name, UserPK executer )ε->void{
		auto resource = SelectResource( name, executer, true );
		if( !resource.at("deleted").is_null() )
			Restore( "resources", GetId(resource), executer );
	}

	α AclTests::SetUpTestCase()ε->void{
		array<string,10> users{ "intruder", "creator", "reader", "updater", "deleter", "purger", "admin", "subscriber", "executor", "root" };
		let resourceSlug = "groups";
		let resource = SelectResource( resourceSlug, GetRoot(), true );
		_resourcePK = ResourcePK{ GetId(resource) };
		if( resource.at("deleted").is_null() )
			Delete( "resources", GetId(resource), GetRoot() );

		auto allowed = ERights::None;
		for( let& user : users ){
			let& juser = Tests::Get( "user", user, GetRoot() );
			UserPK userPK{ GetId(juser) };
			_usersPKs.emplace( user, userPK );
			let acl = GetAcl( userPK, resourceSlug, allowed, ERights::None );
			allowed = allowed==ERights::None
				? ERights::Create
				: allowed==ERights::Execute ? ERights::All : (ERights)(underlying(allowed)<<1);
			_users.emplace( user, acl );
		}
	}

	TEST_F( AclTests, DisabledPermissions ){
		let resourceName = "groups";
		let resource = SelectResource( resourceName, GetRoot() );
		if( resource.at("deleted").is_null() )
			Delete( "resources", GetId(resource), GetRoot() );
		let intruderPK = _usersPKs["intruder"];
		let groupId = TestCrud( "group", "DisabledPermission-Test-Member", intruderPK );
		TestAdd( resourceName, groupId, {_usersPKs["intruder"].Value, _usersPKs["creator"].Value, _usersPKs["reader"].Value}, intruderPK );
		TestRemove( resourceName, groupId, {_usersPKs["intruder"].Value, _usersPKs["creator"].Value}, intruderPK );
		TestPurge( resourceName, groupId, intruderPK );
	}

	α AclTests::TestEnabeledPermissions( str resourceName, str slug, UserPK executer )ε{
		let groupId = TestUnauthCrud( resourceName, slug, executer );
		TestUnauthAddRemove( resourceName, groupId, {_usersPKs["intruder"].Value, _usersPKs["creator"].Value, _usersPKs["reader"].Value}, executer );
		TestUnauthPurge( resourceName, groupId, executer );
		EXPECT_THROW( CreateAcl(_usersPKs["intruder"], ERights::All, ERights::None, resourceName, executer), Exception );
		const RolePK rolePK{ GetId( Get("role", "EnabledPermissionsTest", GetRoot()) ) };
		if( let existingPermission = GetRolePermission( rolePK, resourceName, GetRoot() ); !existingPermission.empty() )
			RemoveRolePermission( rolePK, PermissionPK{GetId(existingPermission)}, GetRoot() );
		EXPECT_THROW( AddRolePermission(rolePK, resourceName, ERights::All, ERights::None, executer), Exception );
		RestoreResource( "roles", GetRoot() );
		EXPECT_THROW( CreateAcl(_usersPKs["intruder"], rolePK, executer), Exception );
		//review #3 #1 - RoleMAwait's add/remove branches gate on the executer, not only the cycle check.
		const RolePK childRolePK{ GetId( Get("role", "EnabledPermissionsTestChild", GetRoot()) ) };
		EXPECT_THROW( InsertRoleMember(rolePK, childRolePK, executer), Exception );
		EXPECT_THROW( RemoveRoleMember(rolePK, childRolePK, executer), Exception );
		EXPECT_TRUE( GetRoleChild(rolePK, childRolePK, GetRoot()).empty() );
		const PermissionPK permissionPK{ GetId(AddRolePermission(rolePK, resourceName, ERights::Read, ERights::None, GetRoot())) };
		EXPECT_THROW( RemoveRolePermission(rolePK, permissionPK, executer), Exception );
		EXPECT_FALSE( GetRolePermission(rolePK, resourceName, GetRoot()).empty() );
		const PermissionPK aclPermissionPK{ GetId(_users["reader"]) };//a direct acl grant, not a role member - access_role_remove would drop its rights row unscoped.
		EXPECT_THROW( RemoveRolePermission(rolePK, aclPermissionPK, executer), Exception );
		EXPECT_EQ( GetId(SelectAcl(_usersPKs["reader"], resourceName)), aclPermissionPK.Value );
		RemoveRolePermission( rolePK, permissionPK, GetRoot() );//an admin still passes the gate.
		EXPECT_TRUE( GetRolePermission(rolePK, resourceName, GetRoot()).empty() );
	}

	TEST_F( AclTests, EnabledPermissions ){
		let resourceName = "groups";
		RestoreResource( resourceName, GetRoot() );
		TestEnabeledPermissions( resourceName, "EnabledPermissions-Group3", _usersPKs["intruder"] );
		TRACET( ELogTags::Test, "EnabledPermissions" );
	}

	TEST_F( AclTests, DeletedUser ){
		let resourceName = "groups";
		RestoreResource( resourceName, GetRoot() );
		auto juser = GetUser( "deletedRoot", GetRoot(), true );
		UserPK executer{ GetId( juser ) };
		GetAcl( executer, "groups", ERights::All, ERights::None );
		if( !Json::FindTimePoint(juser, "deleted") )
			Delete( "users", GetId(juser), GetRoot() );
		TestEnabeledPermissions( resourceName, "AclTests-DeletedUser-Member", executer );
	}

	TEST_F( AclTests, TestHierarchy ){
		let groupResource = "groups";
		RestoreResource( groupResource, GetRoot() );
		let adminGroup = Tests::GetGroup( "HierarchyGroupAdmin", GetRoot() );
		GroupPK adminGroupPK{ GetId( adminGroup ) };

		let userGroup = Tests::GetGroup( "HierarchyGroupUsers", GetRoot() );
		let userGroupMembers = AsArray( userGroup, "groupMembers" );
		GroupPK userGroupPK{ GetId( userGroup ) };
		UserPK hierarchyUser{ GetId( GetUser("hierarchyUser", GetRoot()) ) };
		if( find_if(userGroupMembers, [=](const jvalue& member){ return GetId(Json::AsObject(member))==hierarchyUser.Value; })==userGroupMembers.end() )
			AddToGroup( userGroupPK, {hierarchyUser,adminGroupPK}, GetRoot() );
		const UserPK adminPK{ GetId( GetUser("hierarchyAdmin", GetRoot()) ) };
		let adminGroupMembers = AsArray( adminGroup, "groupMembers" );
		if( find_if(adminGroupMembers, [adminPK](const jvalue& member){ return GetId(Json::AsObject(member))==adminPK.Value; })==adminGroupMembers.end() )
			AddToGroup( adminGroupPK, {adminPK}, GetRoot() );

		const RolePK userRolePK{ GetId( Get("role", "HierarchyGroupUserRole", GetRoot()) ) };
		AddRolePermission( userRolePK, groupResource, ERights::Read, ERights::None, GetRoot() );
		const RolePK adminRolePK{ GetId( Get("role", "HierarchyGroupAdminRole", GetRoot()) ) };
		AddRolePermission( adminRolePK, groupResource, ERights::All & ~ERights::Read, ERights::None, GetRoot() );
		AddRoleMember( adminRolePK, userRolePK, GetRoot() );
		CreateAcl( userGroupPK, userRolePK, GetRoot() );
		CreateAcl( adminGroupPK, adminRolePK, GetRoot() );

		string testGroupSlug{ "hierarchyGroupTest" };
		auto testGroup = SelectGroup( testGroupSlug, hierarchyUser, true );
		if( !testGroup.empty() )
			PurgeGroup( {GetId(testGroup)}, adminPK );
		EXPECT_THROW( Create(groupResource, testGroupSlug, hierarchyUser), Exception );
		Create( groupResource, testGroupSlug, adminPK );
		testGroup = GetGroup( testGroupSlug, hierarchyUser );
		GroupPK testGroupPK{ GetId(testGroup) };
		TestUnauthUpdateName( groupResource, testGroupPK.Value, hierarchyUser, "newName" );
		TestUnauthDeleteRestore( groupResource, testGroupPK.Value, hierarchyUser );
		vector<uint> members{ userGroupPK.Value,adminGroupPK.Value, hierarchyUser.Value, adminPK.Value };
		TestUnauthAddRemove( groupResource, testGroupPK.Value, members, hierarchyUser );
		TestUnauthPurge( groupResource, testGroupPK.Value, hierarchyUser );
		PurgeGroup( testGroupPK, adminPK );

		let testGroupPK2 = TestCrud( groupResource, testGroupSlug, adminPK );
		TestAdd( groupResource, testGroupPK2, members, adminPK );
		TestRemove( groupResource, testGroupPK2, {hierarchyUser.Value, adminPK.Value}, adminPK );
		TestPurge( groupResource, testGroupPK2, adminPK );

		EXPECT_THROW( CreateAcl(_usersPKs["intruder"], ERights::All, ERights::None, groupResource, hierarchyUser), Exception );
		const RolePK rolePK{ GetId( Get("role", "HierarchyPermissionsTest", GetRoot()) ) };
		if( auto permission = GetRolePermission(rolePK, groupResource, GetRoot()); !permission.empty() )
			RemoveRolePermission( rolePK, PermissionPK{GetId(permission)}, GetRoot() );
		EXPECT_THROW( AddRolePermission(rolePK, groupResource, ERights::All, ERights::None, hierarchyUser), Exception );
	}
	TEST_F( AclTests, TestDeny ){
		let resourceName = "groups";
		RestoreResource( resourceName, GetRoot() );

		let deniedGroup = Tests::GetGroup( "DeniedGroup", GetRoot() );
		let deniedGroupPK = GetId( deniedGroup );
		let allowedGroup = Tests::GetGroup( "allowedGroup", GetRoot() );
		let allowedGroupMembers = AsArray( allowedGroup, "groupMembers" );
		let allowedGroupPK = GetId( allowedGroup );
		UserPK executer{ GetId(GetUser("deniedUser", GetRoot())) };
		if( find_if(allowedGroupMembers, [executer](const jvalue& member){ return GetId(Json::AsObject(member))==executer.Value; })==allowedGroupMembers.end() )
			AddToGroup( {allowedGroupPK}, {executer}, GetRoot() );
		let deniedGroupMembers = AsArray( deniedGroup, "groupMembers" );
		if( find_if(deniedGroupMembers, [executer](const jvalue& member){ return GetId(Json::AsObject(member))==executer.Value; })==deniedGroupMembers.end() )
			AddToGroup( {deniedGroupPK}, {executer}, GetRoot() );

		const RolePK deniedRolePK{ GetId( Get("role", "DeniedRole", GetRoot()) ) };
		AddRolePermission( deniedRolePK, resourceName, ERights::None, ERights::All, GetRoot() );
		const RolePK allowedRolePK{ GetId( Get("role", "AllowedRole", GetRoot()) ) };
		AddRolePermission( allowedRolePK, resourceName, ERights::All, ERights::None, GetRoot() );
		CreateAcl( GroupPK{deniedGroupPK}, deniedRolePK, GetRoot() );
		CreateAcl( GroupPK{allowedGroupPK}, allowedRolePK, GetRoot() );
		TestEnabeledPermissions( resourceName, "AclTests-TestDeny-Group", executer );
	}
	TEST_F( AclTests, RemoveRoleChild ){
		let resourceName = "groups";
		RestoreResource( resourceName, GetRoot() );
		UserPK executer{ GetId( GetUser("roleChildUser", GetRoot()) ) };
		const RolePK parentRolePK{ GetId( Get("role", "RoleChildParent", GetRoot()) ) };
		const RolePK childRolePK{ GetId( Get("role", "RoleChildChild", GetRoot()) ) };
		AddRolePermission( childRolePK, resourceName, ERights::All, ERights::None, GetRoot() );//rights live on the child only.
		AddRoleMember( parentRolePK, childRolePK, GetRoot() );
		CreateAcl( executer, parentRolePK, GetRoot() );
		TestPurge( resourceName, TestCrud(resourceName, "AclTests-RoleChild-Group", executer), executer );//inherited through the child.

		RemoveRoleMember( parentRolePK, childRolePK, GetRoot() );
		TestUnauthPurge( resourceName, TestUnauthCrud(resourceName, "AclTests-RoleChild-Group", executer), executer );
	}
	TEST_F( AclTests, PurgeAcl ){
		let resourceName = "groups";
		RestoreResource( resourceName, GetRoot() );
		UserPK executer{ GetId( GetUser("purgeAclUser", GetRoot()) ) };
		let permissionPK = PermissionPK{Json::AsNumber<PermissionPK::Type>( GetAcl(executer, resourceName, ERights::All, ERights::None), "id" )};
		let groupId = TestCrud( resourceName, "AclTests-PurgeAcl-Group", executer );
		TestPurge( resourceName, groupId, executer );
		PurgeAcl( executer, permissionPK, GetRoot() );
		let groupId2 = TestUnauthCrud( resourceName, "AclTests-PurgeAcl-Group", executer );
		TestUnauthPurge( resourceName, groupId2, executer );
	}
	//access-review3 #5:  the permissionRight change subscription was registered under `permissions` while updatePermissionRight
	//publishes under `permission_rights`, so AccessListener::PermissionUpdated never fired and a grant narrowed through the UI kept
	//its old rights in the cache until restart.  ql-review3 #8 re-keyed it; SubscriptionTests pins the fan-out with an explicit
	//subscription, this pins the startup registration end to end - the mutation, then Authorize::Rights.
	TEST_F( AclTests, UpdatePermissionRightReachesTheCache ){
		let resourceName = "groups";
		RestoreResource( resourceName, GetRoot() );
		UserPK executer{ GetId( GetUser("permissionUpdateUser", GetRoot()) ) };
		let permissionPK = PermissionPK{Json::AsNumber<PermissionPK::Type>( GetAcl(executer, resourceName, ERights::All, ERights::None), "id" )};
		ASSERT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::All );
		auto update = [&]( sv args ){ QL().QuerySync<jvalue>( Ƒ("mutation updatePermissionRight( id:{}, {} )", permissionPK.Value, args), {}, GetRoot() ); };

		update( "allowed:0, denied:0" );
		EXPECT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::None ); //revoked - without a restart.
		update( Ƒ("allowed:{}", underlying(ERights::All)) ); //widened, denied omitted - a partial update must leave the other side alone.
		EXPECT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::All );
		update( Ƒ("denied:{}", underlying(ERights::Update)) );
		EXPECT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::All & ~ERights::Update );
		PurgeAcl( executer, permissionPK, GetRoot() );
	}
	//access-review3 #6:  Permission's jobject ctor was noexcept but read id/allowed/denied with the throwing Json::AsNumber.  The
	//mutation layer defaults an omitted allowed/denied to 0 and the notification is built from the mutation's args, so a
	//roleAdded without one of them crossed the noexcept boundary in RoleChanged and took the process down with std::terminate -
	//every subscriber's, the AppServer's included.  Drive the real listener with those payloads directly;  the permission pk is
	//invented, the cache is the subject.
	TEST_F( AclTests, PartialPermissionRightPayloadDoesNotTerminate ){
		let resourceName = "groups";
		RestoreResource( resourceName, GetRoot() );
		let resourcePK = GetId( SelectResource(resourceName, GetRoot()) );
		UserPK executer{ GetId( GetUser("partialPayloadUser", GetRoot()) ) };
		const RolePK rolePK{ GetId( Get("role", "PartialPayloadRole", GetRoot()) ) };
		CreateAcl( executer, rolePK, GetRoot() );
		ASSERT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::None ); //the role holds nothing yet.

		AccessListener listener{ QLPtr() };
		let roleAdded = (QL::SubscriptionId)underlying( ESubscription::Role | ESubscription::Added );
		auto notify = [&]( jobject permissionRight ){
			permissionRight["resource"] = jobject{ {"id", resourcePK} };
			listener.OnChange( jvalue{jobject{{"roleAdded", jobject{{"id", rolePK.Value}, {"permissionRight", move(permissionRight)}}}}}, roleAdded );
		};
		constexpr PermissionPK fakePK{ 987654321 };
		notify( {{"id", fakePK.Value}, {"allowed", underlying(ERights::Read)}} ); //denied omitted - used to terminate here.
		EXPECT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::Read );
		notify( {{"id", fakePK.Value}, {"denied", underlying(ERights::Read)}} ); //allowed omitted - None, as the mutation would have stored.
		EXPECT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::None );
		EXPECT_THROW( notify({{"allowed", underlying(ERights::All)}}), Exception ); //id omitted - unusable:  refused, not cached under pk 0, not terminated on.
		EXPECT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::None );
		//take the invented permission back out through the Removed path.  The role and its acl row stay (Get/CreateAcl are
		//get-or-create):  access_role_purge trips the acl fk on a role that is still assigned - finding 13, not this one.
		listener.OnChange( jvalue{jobject{{"roleRemoved", jobject{{"id", rolePK.Value}, {"permissionRight", jobject{{"id", fakePK.Value}}}}}}}, (QL::SubscriptionId)underlying(ESubscription::Role | ESubscription::Removed) );
		EXPECT_EQ( Authorizer()->Rights("access", resourceName, executer), ERights::None );
	}
	//access-review3 #11:  purgeAcl gated on which key the client sent - role:{id} meant Administer of `roles`, whatever the pk was -
	//so a roles admin could revoke any identity's direct grant on any resource by spelling its pk as a role.  The gate now comes
	//from access_permissions.is_role, either spelling is accepted, and the notification carries the key the pk is, so the cache's
	//RemoveAcl finds the right entry whichever the client sent.
	TEST_F( AclTests, PurgeAclGatesOnWhatThePkIs ){
		RestoreResource( "roles", GetRoot() );
		RestoreResource( "groups", GetRoot() );
		UserPK rolesAdmin{ GetId( GetUser("purgeAclRolesAdmin", GetRoot()) ) };
		UserPK groupsAdmin{ GetId( GetUser("purgeAclGroupsAdmin", GetRoot()) ) };
		UserPK victim{ GetId( GetUser("purgeAclVictim", GetRoot()) ) };
		let rolesAdminPK = GetId( GetAcl(rolesAdmin, "roles", ERights::Administer, ERights::None) );
		let groupsAdminPK = GetId( GetAcl(groupsAdmin, "groups", ERights::Administer, ERights::None) );
		let grantPK = GetId( GetAcl(victim, "groups", ERights::Read, ERights::None) ); //a direct grant...
		const RolePK rolePK{ GetId( Get("role", "PurgeAclGateRole", GetRoot()) ) };
		AddRolePermission( rolePK, "groups", ERights::Update, ERights::None, GetRoot() );
		CreateAcl( victim, rolePK, GetRoot() ); //...and a role assignment, in the same pk space.
		ASSERT_EQ( Authorizer()->Rights("access", "groups", victim), ERights::Read | ERights::Update );
		auto purge = [&]( IdentityPK identity, sv key, uint pk, UserPK executer ){
			BlockTAwait<jvalue>( Server::AclQLAwait{QL::ParseM(Ƒ("purgeAcl( identity:{{ id:{} }}, {}:{{ id:{} }} )", identity.Underlying(), key, pk), {}, Schemas()), executer} );
		};

		EXPECT_THROW( purge(victim, "role", grantPK, rolesAdmin), Exception ); //the finding:  a direct grant spelled as a role.
		EXPECT_EQ( GetId(SelectAcl(victim, "groups")), grantPK );
		EXPECT_THROW( purge(victim, "permissionRight", rolePK.Value, groupsAdmin), Exception ); //the mirror:  a role spelled as a permission.
		EXPECT_FALSE( SelectAcl(victim, rolePK).empty() );
		EXPECT_EQ( Authorizer()->Rights("access", "groups", victim), ERights::Read | ERights::Update );

		purge( victim, "permissionRight", rolePK.Value, rolesAdmin ); //the right admin under the "wrong" spelling - what the pk is decides.
		EXPECT_TRUE( SelectAcl(victim, rolePK).empty() );
		EXPECT_EQ( Authorizer()->Rights("access", "groups", victim), ERights::Read ); //and the cache dropped the role, not a permission of that pk.
		purge( victim, "role", grantPK, groupsAdmin );
		EXPECT_TRUE( SelectAcl(victim, "groups").empty() );
		EXPECT_EQ( Authorizer()->Rights("access", "groups", victim), ERights::None );
		PurgeAcl( rolesAdmin, PermissionPK{rolesAdminPK}, GetRoot() );
		PurgeAcl( groupsAdmin, PermissionPK{groupsAdminPK}, GetRoot() );
	}
	//access-review3 #12:  access_ac_upsert_permission looked the existing grant up with `criteria is null` - a column only
	//access_resources has, and one resource_id already pins - so for a criteria-scoped resource the lookup never matched, every
	//createAcl minted a new permission, and the identity's rights became the OR of every grant ever made (User::operator+= ORs
	//allowed and denied), live and after a reload.  Criteria-null resources - everything the UI reaches - were unaffected.
	TEST_F( AclTests, RegrantOnCriteriaResourceUpserts ){
		let root = GetRoot();
		const UserPK system{ UserPK::System }; //grants on a resource root holds no rights over - System early-passes TestAdmin.
		constexpr sv schema{ "access" }, slug{ "aclUpsert" }, criteria{ "nodeId:{ eq: 12 }" }; //in `access` (the resources subscription used to be filtered to it - it takes every schema now); criteria-scoped, so CheckDefaults' criteria:null count is untouched.
		let select = Ƒ( R"(resources( schemaName:"{}", slug:"{}", criteria:"{}" ){{ id }})", schema, slug, criteria );
		auto resources = QL().QuerySync<jarray>( select, {}, root );
		if( resources.empty() ){
			QL().QuerySync<jvalue>( Ƒ(R"(createResource( schemaName:"{}", name:"{}", slug:"{}", criteria:"{}", allowed:255 ))", schema, slug, slug, criteria), {}, system );
			resources = QL().QuerySync<jarray>( select, {}, root );
		}
		ASSERT_EQ( resources.size(), 1u );
		const ResourcePK resourcePK{ GetId(Json::AsObject(resources[0])) };
		const UserPK user{ GetId(GetUser("aclUpsertUser", root)) };
		auto grant = [&]( ERights allowed )->PermissionPK{
			let q = Ƒ( "createAcl( identity:{{ id:{} }}, permissionRight:{{ allowed:{}, denied:0, resource:{{ id:{} }} }} ){{ permissionRight{{id}} }}", user.Value, underlying(allowed), resourcePK.Value );
			return PermissionPK{Json::AsNumber<PermissionPK::Type>( BlockTAwait<jvalue>( Server::AclQLAwait{QL::ParseM(q, {}, Schemas()), system} ).as_object(), "permissionRight/id" )};
		};
		auto aclRows = [&]{ return QL().QuerySync<jarray>( Ƒ("acl( identityId:{} ){{ identityId permissionRight{{ id }} }}", user.Value), {}, root ).size(); };

		let first = grant( ERights::Read | ERights::Administer );
		ASSERT_EQ( aclRows(), 1u );
		EXPECT_NO_THROW( Authorizer()->TestAdminResource(resourcePK, user) );
		let second = grant( ERights::Read ); //narrowed by a re-grant - the upsert the proc's name promises.
		EXPECT_EQ( second, first ) << "the same (identity, resource) has to come back as the same permission";
		EXPECT_EQ( aclRows(), 1u ) << "not a second acl row";
		EXPECT_THROW( Authorizer()->TestAdminResource(resourcePK, user), Exception ) << "Administer is gone live, not OR'd in from the first grant";
		let reloaded = BlockAwait<AclLoadAwait, flat_multimap<IdentityPK,PermissionRole>>( AclLoadAwait{QLPtr(), system} );
		EXPECT_EQ( reloaded.count(IdentityPK{user}), 1u ) << "and after a reload"; //the user holds nothing else.
		PurgeAcl( user, first, system );
		Purge( "resource", resourcePK, root ); //leave no trace.
	}
	//access-review3 #14's sharper shape:  holding an acl grant or a group membership, purgeUser deleted access_users and then failed
	//on access_identities - an orphan identity row no api could purge, committed on the autocommit dialects.  The users purgeProc
	//takes the children first, so the identity goes cleanly.
	TEST_F( AclTests, PurgeUserWithGrantAndMembership ){
		let root = GetRoot();
		RestoreResource( "groups", root );
		const UserPK user{ GetId(GetUser("purgeUserInUse", root)) };
		GetAcl( user, "groups", ERights::All, ERights::None );
		const GroupPK group{ GetId(GetGroup("purgeUserInUseGroup", root)) };
		AddToGroup( group, {user}, root );
		auto countRows = [&]( str table, sv column ){ let& dbTable = *GetTable( table ); return dbTable.Schema->DS()->ScalerSync<uint>( DB::Sql{Ƒ("select count(*) from {} where {}=?", dbTable.SqlName(), column), {DB::Value{user.Value}}} ); };
		ASSERT_EQ( countRows("acl", "identity_id"), 1u );
		ASSERT_EQ( countRows("groups", "member_id"), 1u );

		EXPECT_NO_THROW( PurgeUser(user, root) );
		EXPECT_TRUE( SelectUser("purgeUserInUse", root, nullopt, true).empty() ) << "no orphaned identity row";
		EXPECT_EQ( countRows("acl", "identity_id"), 0u );
		EXPECT_EQ( countRows("groups", "member_id"), 0u );
		EXPECT_EQ( countRows("identities", "identity_id"), 0u );
		PurgeGroup( group, root );
	}
	//The group half of the same shape:  a group holding an acl grant and nested inside a parent group left the identity row
	//behind for the same reason - purgeGroup deleted only access_groups where identity_id=, then failed on access_identities'
	//fk from the acl row and from its own member_id row in the parent.  access_group_purge takes both first.
	TEST_F( AclTests, PurgeGroupWithGrantAndMembership ){
		let root = GetRoot();
		RestoreResource( "groups", root );
		const GroupPK group{ GetId(GetGroup("purgeGroupInUse", root)) };
		const GroupPK parent{ GetId(GetGroup("purgeGroupInUseParent", root)) };
		CreateAcl( IdentityPK{group}, ERights::All, ERights::None, "groups", root );
		AddToGroup( parent, {group}, root );
		auto countRows = [&]( str table, sv column ){ let& dbTable = *GetTable( table ); return dbTable.Schema->DS()->ScalerSync<uint>( DB::Sql{Ƒ("select count(*) from {} where {}=?", dbTable.SqlName(), column), {DB::Value{group.Value}}} ); };
		ASSERT_EQ( countRows("acl", "identity_id"), 1u );
		ASSERT_EQ( countRows("groups", "member_id"), 1u );

		EXPECT_NO_THROW( PurgeGroup(group, root) );
		EXPECT_TRUE( SelectGroup("purgeGroupInUse", root, true).empty() ) << "no orphaned identity row";
		EXPECT_EQ( countRows("acl", "identity_id"), 0u );
		EXPECT_EQ( countRows("groups", "member_id"), 0u );
		EXPECT_EQ( countRows("identities", "identity_id"), 0u );
		PurgeGroup( parent, root );
	}
	//userRights( id: ) - UserRightsAwait, the Effective rights tab's query.  The walk Authorize::UserRights makes has to say
	//what Rights() enforces, source by source:  a role granted to the grandparent group whose child role holds the grant, and a
	//direct deny on the same resource.
	Ω userRights( UserPK user, UserPK executer )ε->jarray{
		jobject vars{ {"id", user.Value} };
		let q = "userRights( id:$id ){ resource{id} allowed denied effective sources{ permissionId allowed denied path{id type} } }";
		return BlockTAwait<jvalue>( Server::UserRightsAwait{QL::ParseQuery(q, vars, Schemas()), executer} ).as_array();
	}
	Ω findResourceRights( const jarray& rows, ResourcePK resourcePK )ε->jobject{
		auto p = find_if( rows, [=](const jvalue& row){ return ResourcePK{AsNumber<ResourcePK::Type>(Json::AsObject(row), "resource/id")}==resourcePK; } );
		return p==rows.end() ? jobject{} : p->as_object();
	}
	TEST_F( AclTests, UserRightsProvenance ){
		let root = GetRoot();
		RestoreResource( "groups", root );
		const UserPK user{ GetId(GetUser("userRightsUser", root)) };
		let group = GetGroup( "userRightsGroup", root );
		const GroupPK groupPK{ GetId(group) };
		let parent = GetGroup( "userRightsParent", root );
		const GroupPK parentPK{ GetId(parent) };
		auto addMember = [&]( const jobject& g, GroupPK gPK, IdentityPK member ){ //addGroup is not idempotent - TestHierarchy's guard.
			let members = AsArray( g, "groupMembers" );
			if( find_if(members, [=](const jvalue& m){ return GetId(Json::AsObject(m))==member.Underlying(); })==members.end() )
				AddToGroup( gPK, {member}, root );
		};
		addMember( group, groupPK, user );
		addMember( parent, parentPK, groupPK );
		const RolePK parentRolePK{ GetId( Get("role", "userRightsParentRole", root) ) };
		const RolePK childRolePK{ GetId( Get("role", "userRightsChildRole", root) ) };
		const PermissionPK permissionPK{ GetId(AddRolePermission(childRolePK, "groups", ERights::Read|ERights::Update, ERights::None, root)) };
		AddRoleMember( parentRolePK, childRolePK, root );
		CreateAcl( parentPK, parentRolePK, root );
		const PermissionPK denyPK{ GetId(GetAcl(user, "groups", ERights::None, ERights::Update)) };

		auto check = [&]( UserPK executer ){
			let row = findResourceRights( userRights(user, executer), _resourcePK );
			ASSERT_FALSE( row.empty() );
			EXPECT_EQ( AsNumber<uint8>(row, "allowed"), underlying(ERights::Read|ERights::Update) );
			EXPECT_EQ( AsNumber<uint8>(row, "denied"), underlying(ERights::Update) );
			EXPECT_EQ( AsNumber<uint8>(row, "effective"), underlying(ERights::Read) );
			EXPECT_EQ( Authorizer()->Rights("access", "groups", user), ERights::Read );//the guarantee:  the tab shows what enforcement answers.
			let sources = AsArray( row, "sources" );
			ASSERT_EQ( sources.size(), 2u );
			auto source = [&]( PermissionPK pk )->jobject{
				auto p = find_if( sources, [=](const jvalue& s){ return PermissionPK{AsNumber<PermissionPK::Type>(Json::AsObject(s), "permissionId")}==pk; } );
				return p==sources.end() ? jobject{} : p->as_object();
			};
			let viaRole = source( permissionPK );
			ASSERT_FALSE( viaRole.empty() );
			let path = AsArray( viaRole, "path" );
			ASSERT_EQ( path.size(), 4u ) << "top-down: the granted group, the group holding the user, the assigned role, the role holding the permission";
			const array<std::pair<uint32,string>,4> expected{ {{parentPK.Value, "group"}, {groupPK.Value, "group"}, {parentRolePK.Value, "role"}, {childRolePK.Value, "role"}} };
			for( uint i=0; i<expected.size(); ++i ){
				let step = Json::AsObject( path[i] );
				EXPECT_EQ( GetId(step), expected[i].first ) << i;
				EXPECT_EQ( AsString(step, "type"), expected[i].second ) << i;
			}
			let direct = source( denyPK );
			ASSERT_FALSE( direct.empty() );
			EXPECT_TRUE( AsArray(direct, "path").empty() );
			EXPECT_EQ( AsNumber<uint8>(direct, "denied"), underlying(ERights::Update) );
		};
		check( root );
		check( user );//one's own rights need no grant.

		Delete( "groups", groupPK.Value, root );//a deleted group stops the walk, as it stops enforcement.
		{
			let row = findResourceRights( userRights(user, root), _resourcePK );
			ASSERT_FALSE( row.empty() );
			EXPECT_EQ( AsArray(row, "sources").size(), 1u ) << "only the direct deny";
			EXPECT_EQ( AsNumber<uint8>(row, "effective"), 0u );
			EXPECT_EQ( Authorizer()->Rights("access", "groups", user), ERights::None );
		}
		Restore( "groups", groupPK.Value, root );
		check( root );

		PurgeAcl( user, denyPK, root );
		RemoveRoleMember( parentRolePK, childRolePK, root );
	}
	//The gate is acl( identityId: )'s - Read on the acl resource - except for one's own row.  Enabled for the test only:
	//ReadAuthorization below asserts the row is disabled at its start, so this leaves it as the sync made it.
	TEST_F( AclTests, UserRightsGate ){
		auto resource = SelectResource( "acl", GetRoot(), true );
		ASSERT_FALSE( resource.at("deleted").is_null() ) << "created disabled, like every synced resource";
		let permissionPK = CreateAcl( GetRoot(), ERights::All, ERights::None, "acl", {UserPK::System} );
		RestoreResource( "acl", GetRoot() );
		let intruder = _usersPKs["intruder"];
		let reader = _usersPKs["reader"];
		EXPECT_THROW( userRights(reader, intruder), Exception );
		EXPECT_NO_THROW( userRights(reader, GetRoot()) );
		EXPECT_NO_THROW( userRights(intruder, intruder) );
		Delete( "resources", GetId(resource), GetRoot() );
		EXPECT_NO_THROW( userRights(reader, intruder) );//fail-open when disabled, as the acl read is.
		PurgeAcl( GetRoot(), permissionPK, GetRoot() );
	}
	//access-review3 #21:  acl was ops:["None"], so ResourceSync never created its resource row and the read gate below had nothing
	//to gate with - this test used to hand-create the row, which is exactly what no deployment does.  acl has ops now, so the
	//row is there from the sync, disabled like every other, and an operator enables the gate by restoring it.
	TEST_F( AclTests, ReadAuthorization ){
		auto resource = SelectResource( "acl", GetRoot(), true );
		ASSERT_FALSE( resource.empty() ) << "ResourceSync has to create the acl resource";
		ASSERT_FALSE( resource.at("deleted").is_null() ) << "created disabled, like every synced resource";
		let permissionPK = CreateAcl( GetRoot(), ERights::All, ERights::None, "acl", {UserPK::System} ); //grant root while disabled.
		RestoreResource( "acl", GetRoot() );
		let intruder = _usersPKs["intruder"];
		let q = "acl( identityId:$identityId ){ identityId permissionRight{id} }";
		jobject vars{ {"identityId", intruder.Value} };
		EXPECT_THROW( BlockTAwait<jvalue>( Server::AclQLSelectAwait{QL::ParseQuery(q, vars, Schemas()), intruder} ), Exception );
		BlockTAwait<jvalue>( Server::AclQLSelectAwait{QL::ParseQuery(q, vars, Schemas()), GetRoot()} );
		Delete( "resources", GetId(resource), GetRoot() );
		BlockTAwait<jvalue>( Server::AclQLSelectAwait{QL::ParseQuery(q, vars, Schemas()), intruder} ); //fail-open when disabled.
		PurgeAcl( GetRoot(), permissionPK, GetRoot() );
		//the row stays, disabled, as the sync left it - ResourceTests.CheckDefaults counts it now.
	}
	//remove user from group/role.

	//access-refactor A8:  the json each acl( … ){ <child> } select returns - the shapes access-service.ts and SelectAcl read.  The
	//child table's columns nest under its singular, its pk renamed `id`;  acl's own columns, and the identities join's, go
	//under `identity`.  An identities child is the exception:  rows under `identities`, shaped by the query.
	TEST_F( AclTests, SelectShapes ){
		let root = GetRoot();
		let userJson = GetUser( "aclShapeUser", root );
		const UserPK user{ GetId(userJson) };
		let name = Json::AsString( userJson, "name" );
		const RolePK rolePK{ GetId(Get("role", "aclShapeRole", root)) };
		let grant = CreateAcl( user, ERights::Read, ERights::Update, "groups", root );
		CreateAcl( user, rolePK, root );
		let groupsPK = ResourcePK{AsNumber<ResourcePK::Type>( SelectResource("groups", root, true), "id" )};
		let select = [&]( string ql ){ return BlockTAwait<jvalue>( Server::AclQLSelectAwait{QL::ParseQuery(ql, {}, Schemas()), root} ); };
		let expect = [&]( string args, string children, string json ){ EXPECT_EQ( select(Ƒ("acl( {} ){{ {} }}", args, children)), parse(json) ) << children; };
		let byUser = Ƒ( "identityId:{}", user.Value );
		expect( byUser, "identityId permissionRight{ id allowed denied resource{ id slug deleted } }",
			Ƒ(R"([{{"permissionRight":{{"id":{},"allowed":2,"denied":4,"resource":{{"id":{},"slug":"groups","deleted":null}}}},"identity":{{"id":{}}}}}])", grant.Value, groupsPK.Value, user.Value) );
		expect( byUser, "permissionRights{ id resource{ id deleted } }",
			Ƒ(R"([{{"permissionRight":{{"id":{},"resource":{{"id":{},"deleted":null}}}}}}])", grant.Value, groupsPK.Value) );
		expect( byUser, "identityId role{ id slug deleted }",
			Ƒ(R"([{{"role":{{"id":{},"slug":"aclShapeRole","deleted":null}},"identity":{{"id":{}}}}}])", rolePK.Value, user.Value) );
		expect( byUser, "identities{ name } permissionRight{ id }",
			Ƒ(R"([{{"permissionRight":{{"id":{}}},"identity":{{"name":"{}"}}}}])", grant.Value, name) );
		expect( Ƒ("permissionId:{}", grant.Value), "identities{ id isGroup }",
			Ƒ(R"([{{"identities":[{{"id":{},"isGroup":false}}]}}])", user.Value) );
		expect( byUser, "identityId permissions{ id }",
			Ƒ(R"([{{"permissions":{{"id":{}}},"identityId":{}}},{{"permissions":{{"id":{}}},"identityId":{}}}])", std::min<uint>(rolePK.Value, grant.Value), user.Value, std::max<uint>(rolePK.Value, grant.Value), user.Value) );

		PurgeAcl( user, grant, root );
		Purge( "role", rolePK, root );
		PurgeUser( user, root );
	}
}
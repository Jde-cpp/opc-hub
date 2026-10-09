//GHSA-g354-grf2-r8vh:  the tables that define rights - identities, providers, permissions, permissionRights, roleMembers,
//rights, seeds - declare ops:["None"], and resources declares Delete alone.  Table::Authorize tested only the rights a table
//declares, so a generic create/update/delete/purge on them was refused to no one - the unauthenticated UserPK{0} included -
//and ResourceLoadAwait makes no resource row for an undeclared op, so enforcing every resource could not close it.  Such a
//write is the system's alone now (IAcl::TestSystem); reads outside the ops stay open, and an extension's purge still covers
//the row it extends.  Two have callers that are not the system and take an admin check instead (AdminWriteAwait.h):
//updatePermissionRight and resources' create/update.
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
		let id = _intruder.Value;
		const vector<string> writes{
			"mutation createIdentity( name:\"ghsa-g354\", slug:\"ghsa-g354\" )",
			Ƒ( "mutation updateIdentity( id:{}, description:\"ghsa-g354\" )", id ),
			Ƒ( "mutation deleteIdentity( id:{} )", id ),
			Ƒ( "mutation purgeIdentity( id:{} )", id ),
			Ƒ( "mutation createProvider( slug:\"ghsa-g354\", providerType:{} )", underlying(EProviderType::Key) ),
			"mutation updateProvider( id:1, slug:\"ghsa-g354\" )",
			"mutation deleteProvider( id:1 )",
			"mutation purgeProvider( id:1 )",
			"mutation createPermission( isRole:false )",
			"mutation updatePermission( id:1, isRole:true )",
			"mutation purgePermission( id:1 )",
			"mutation createPermissionRight( permissionId:1, resourceId:1, allowed:255, denied:0 )",
			"mutation purgePermissionRight( id:1 )",
			"mutation createRoleMember( roleId:1, memberId:1 )",
			"mutation addRoleMember( id:1, memberId:[1] )",
			"mutation removeRoleMember( id:1, memberId:[1] )",
			"mutation purgeRoleMember( id:1 )",
			"mutation createRights( id:8, name:\"ghsa-g354\" )",
			"mutation updateRights( id:1, name:\"ghsa-g354\" )",
			"mutation purgeRights( id:1 )",
			"mutation createSeeds( name:\"ghsa-g354\", contentHash:\"0\", applied:\"2026-10-09T00:00:00Z\" )",//plural, as rights:  Names::IsPlural reads the -ed of `seed` as a plural, so the singular resolves no table.
			"mutation updateSeeds( name:\"access.roles\", contentHash:\"0\" )",
			"mutation purgeResource( id:1 )"//create/update: ResourceWritesNeedTheSchemasAdmin.
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
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(update, {}, root) );//root administers every resource.
		EXPECT_EQ( Authorizer()->Rights("access", resource, holder), ERights::All ) << "and the admin's reached the cache";
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

	//The controls:  the system still writes them - the seeds, ResourceLoadAwait and the hub's provider insert all run as System -
	//and a read outside a table's ops is as open as before:  the rights enum every permission table renders.
	TEST_F( UndeclaredWriteTests, TheSystemWritesAndAnyoneReads ){
		const UserPK system{ UserPK::System };
		let id = GetId( QL().QuerySync(Ƒ("createProvider( slug:\"ghsa-g354-control\", providerType:{} ){{ id }}", underlying(EProviderType::Key)), {}, system) );
		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation purgeProvider( id:{} )", id), {}, system) );
		EXPECT_FALSE( QL().QuerySync<jarray>("rights{ id name }", {}, _intruder).empty() );
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

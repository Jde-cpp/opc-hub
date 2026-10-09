//ql-review3 #10:  SelectAwait authorized _qlTable.DBTable(), which for a table declaring `qlView` is the *view* - users ->
//users_ql -> resource "usersQl", a name ResourceLoadAwait never creates (it makes one per table, slug ToJson(table->Name)).
//Authorize::Test returns early for a resource it cannot find, so Read on users/providers/connections was never enforced.
//View now carries an owner back-pointer and tests the owner's name, and SelectAwait authorizes the whole requested tree
//rather than only its root.  This is the suite with a real authorizer and a real schema, so the coverage lives here.
#include "gtest/gtest.h"
#include <jde/access/Authorize.h>
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	//A resource is enforced only while it is *not* deleted (Authorize::UpdateResourceDeleted erases it from SchemaResources
	//on delete), and the suite shares one database - so put it back the way it was found.
	struct QlViewAuthorizeTests : ::testing::Test{
		α SetUp()->void override{
			let resource = SelectResource( "users", GetRoot(), true );
			_resourcePK = GetId( resource );
			_wasDeleted = !resource.at( "deleted" ).is_null();
			if( _wasDeleted )
				Restore( "resources", _resourcePK, GetRoot() );
			_intruder = UserPK{ GetId(Tests::Get("user", "review10-intruder", GetRoot())) };
		}
		α TearDown()->void override{
			if( _wasDeleted )
				Delete( "resources", _resourcePK, GetRoot() );
		}
		uint32 _resourcePK{};
		bool _wasDeleted{};
		UserPK _intruder;
	};

	//the finding's shape:  users declares qlView "users_ql" ([access-meta.jsonnet] qlView), so the resource tested used to be
	//"usersQl" - which does not exist, so every principal read every user.
	TEST_F( QlViewAuthorizeTests, ReadOfAQlViewedTableIsEnforced ){
		EXPECT_THROW( QL().QuerySync<jarray>("users{ id loginName }", {}, _intruder), Exception );
		EXPECT_NO_THROW( QL().QuerySync<jarray>("users{ id loginName }", {}, GetRoot()) ); //the control: root still reads.
	}

	//a table without a qlView was always enforced;  it must stay that way, and through the same code path.
	TEST_F( QlViewAuthorizeTests, ReadOfAPlainTableIsStillEnforced ){
		let resource = SelectResource( "groups", GetRoot(), true );
		let wasDeleted = !resource.at( "deleted" ).is_null();
		if( wasDeleted )
			Restore( "resources", GetId(resource), GetRoot() );
		EXPECT_THROW( QL().QuerySync<jarray>("groups{ id slug }", {}, _intruder), Exception );
		if( wasDeleted )
			Delete( "resources", GetId(resource), GetRoot() );
	}

	//#10's other half:  columnSql joins the fk children into the parent's statement and SelectSubTables selects the rest -
	//neither authorized anything, so a child table was readable through a parent the caller could read.
	//providers{ providerType{} }, not users{ groups{} } or permissionRights{ resource{} }:  Access::Server::CustomQuery intercepts
	//user*/group*/role*/acl/profiles before SelectAwait ever sees them, so those pairs prove nothing about this path;  and
	//resources declares Delete/Subscribe alone, so any known user reads it.  providers (its view - the root lets the intruder past) reaches the stock select, and its
	//`providerType` child is the fk join columnSql builds into the same statement, on a table with the default ops.
	TEST_F( QlViewAuthorizeTests, ChildTableIsAuthorizedToo ){
		let resource = SelectResource( "providerTypes", GetRoot(), true );
		let wasDeleted = !resource.at( "deleted" ).is_null();
		if( wasDeleted )
			Restore( "resources", GetId(resource), GetRoot() );

		constexpr sv ql{ "providers{ id providerType{ id name } }" };
		EXPECT_NO_THROW( QL().QuerySync<jarray>(string{ql}, {}, GetRoot()) ) << "the query itself has to be valid, or the throw below proves nothing";
		try{
			QL().QuerySync<jarray>( string{ql}, {}, _intruder );
			ADD_FAILURE() << "the child table was read without authorization";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("providerTypes"), string::npos ) << e.what(); //refused for the child, not for something else.
		}

		if( wasDeleted )
			Delete( "resources", GetId(resource), GetRoot() );
	}

	//access-review3 #3's other route:  users{ … groups{} } is Server::CustomQuery's UserAwait, which runs two stock selects -
	//groups, then the qlView'd users - and the second is the one that used to test "usersQl".  groups is disabled here so the
	//only table that can refuse the intruder is users.
	TEST_F( QlViewAuthorizeTests, CustomUserPathIsEnforcedToo ){
		let groups = SelectResource( "groups", GetRoot(), true );
		let groupsWasActive = groups.at( "deleted" ).is_null();
		if( groupsWasActive )
			Delete( "resources", GetId(groups), GetRoot() );

		constexpr sv ql{ "users{ id loginName groups{ id } }" };
		EXPECT_NO_THROW( QL().QuerySync<jarray>(string{ql}, {}, GetRoot()) ) << "the query itself has to be valid, or the throw below proves nothing";
		try{
			QL().QuerySync<jarray>( string{ql}, {}, _intruder );
			ADD_FAILURE() << "users was read through UserAwait without authorization";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("users"), string::npos ) << e.what(); //refused for users, not for groups.
		}

		if( groupsWasActive )
			Restore( "resources", GetId(groups), GetRoot() );
	}
}

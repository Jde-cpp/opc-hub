//ql-review3 #17:  the process-killers this review found are all reachable from client-shaped input, and the ones that need a
//real schema had no test at all.  Jde.QL.Tests pins the schema-free half (ParserTests, FilterTests, TableQLTests); this is the
//half that needs columns behind it - TableQL::SetResult is Ι and takes a DB::Column, so it is only reachable through a real
//select.  Nothing here asserts an error:  the assertion is that the process is still running afterwards.
#include "gtest/gtest.h"
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	α CreateAcl( IdentityPK identityPK, ERights allowed, ERights denied, string resource, UserPK executer )ε->PermissionPK;//AclTests.cpp
	α PurgeAcl( IdentityPK identityPK, PermissionPK permissionPK, UserPK executer )ε->void;//AclTests.cpp

	//permission_rights rows come from acls, and whether any exist when this suite runs depends on test order - so make one.
	struct HostileInputTests : ::testing::Test{
		α SetUp()->void override{
			_user = UserPK{ GetId(Tests::Get("user", "review17-user", GetRoot())) };
			_permission = CreateAcl( IdentityPK{_user}, ERights::Read, ERights::None, "groups", GetRoot() );
		}
		α TearDown()->void override{
			PurgeAcl( IdentityPK{_user}, _permission, GetRoot() );
			PurgeUser( _user, GetRoot() );
		}
		Jde::UserPK _user;
		PermissionPK _permission{};
	};
	//#5(c)/#17: `resource` is both a column - the bare fk stem, which addColumn resolves through resource_id to resources.name
	//and writes as a *string* - and a sub-table of the same name, whose SetResult then wanted an object at that key.  SetResult
	//is Ι, so as_object() on that string was a terminate rather than a failed query.  The assertion that matters here is that
	//the process is still running;  what it returns is the column half, which is what was asked for.
	TEST_F( HostileInputTests, ColumnAndSubTableSharingANameSurvives ){
		let rows = QL().QuerySync<jarray>( "permissionRights{ id resource resource{ id name deleted } }", {}, GetRoot() );
		ASSERT_FALSE( rows.empty() );
		let& row = Json::AsObject( rows[0] );
		EXPECT_TRUE( row.contains("id") );
		EXPECT_TRUE( row.at("resource").is_string() ) << serialize( row );//the column half answered; the sub-table is skipped for the row.
	}
	//the halves on their own - so the survival above is the collision being handled rather than the query quietly failing.
	TEST_F( HostileInputTests, EitherHalfAloneStillWorks ){
		let column = QL().QuerySync<jarray>( "permissionRights{ id resource }", {}, GetRoot() );
		ASSERT_FALSE( column.empty() );
		EXPECT_TRUE( Json::AsObject(column[0]).at("resource").is_string() );

		let subTable = QL().QuerySync<jarray>( "permissionRights{ id resource{ id name deleted } }", {}, GetRoot() );
		ASSERT_FALSE( subTable.empty() );
		EXPECT_TRUE( Json::AsObject(subTable[0]).at("resource").is_object() );
	}
}

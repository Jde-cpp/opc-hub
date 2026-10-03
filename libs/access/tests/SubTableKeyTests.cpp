//ql-review3 #22: sub-table rows are keyed by the parent's pk, but Query read it from select column *0* while columnSql emits
//the select in the order the client asked for its columns.  A string there logged `Assert: Value is a 'string', not a number`
//and returned 0, and addSubTables' fallback then attached the lowest-keyed parent's children to every row - so `roles{ name id
//permissions{ id } }` gave every role the first role's permissions.
#include "gtest/gtest.h"
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	α AddRolePermission( RolePK rolePK, sv resourceName, ERights allowed, ERights denied, UserPK executer )ε->jobject;//RoleTests.cpp

	//two parents with *different* children, which is what makes a mis-keyed lookup visible at all.
	struct SubTableKeyTests : ::testing::Test{
		α SetUp()->void override{
			//`permissions`, not `roles`, as the child:  Access::Server::CustomQuery intercepts a roles query that names a roles
			//sub-table and answers it from RoleAwait, which never reaches SelectAwait at all.
			_parentA = RolePK{ GetId(Get("role", "review22-parentA", GetRoot())) };
			_parentB = RolePK{ GetId(Get("role", "review22-parentB", GetRoot())) };
			_childA = PermissionPK{ GetId(AddRolePermission(_parentA, "users", ERights::Read, ERights::None, GetRoot())) };
			_childB = PermissionPK{ GetId(AddRolePermission(_parentB, "groups", ERights::Read, ERights::None, GetRoot())) };
		}
		α TearDown()->void override{
			for( let role : {_parentA, _parentB} )
				Purge( "role", role, GetRoot() );
		}
		//<pk of the role, its listed member ids>
		Ω members( str ql )ε->flat_map<uint,vector<uint>>{
			flat_map<uint,vector<uint>> y;
			for( let& row : QL().QuerySync<jarray>(ql, {}, GetRoot()) ){
				let& o = Json::AsObject( row );
				vector<uint> ids;
				for( let& member : Json::AsArray(o, "permissions") )
					ids.push_back( Json::AsNumber<uint>(Json::AsObject(member), "id") );
				y.emplace( Json::AsNumber<uint>(o, "id"), move(ids) );
			}
			return y;
		}
		RolePK _parentA{}, _parentB{};
		PermissionPK _childA{}, _childB{};
	};

	TEST_F( SubTableKeyTests, ChildrenFollowTheirOwnParent ){
		let idFirst = members( "roles{ id slug permissions{ id } }" );//the shipped angular order - correct before the fix too.
		ASSERT_TRUE( idFirst.contains(_parentA.Value) && idFirst.contains(_parentB.Value) );
		EXPECT_EQ( idFirst.at(_parentA.Value), vector<uint>{_childA.Value} );
		EXPECT_EQ( idFirst.at(_parentB.Value), vector<uint>{_childB.Value} );

		//the same query with the columns the other way round:  the pk is no longer select column 0.
		let nameFirst = members( "roles{ slug id permissions{ id } }" );
		ASSERT_TRUE( nameFirst.contains(_parentA.Value) && nameFirst.contains(_parentB.Value) );
		EXPECT_EQ( nameFirst.at(_parentA.Value), vector<uint>{_childA.Value} ) << "parent A got another role's members";
		EXPECT_EQ( nameFirst.at(_parentB.Value), vector<uint>{_childB.Value} ) << "parent B got another role's members";

	}
}

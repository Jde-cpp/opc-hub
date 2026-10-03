//access-refactor B5:  users and groups share access_identities, and is_group says which a row is.  Insert and select applied
//it;  update, delete/restore and purge keyed the identities statement on the id or slug alone.  deleteGroup with a user's id
//soft-deleted the user - authorized against groups, published as groupDeleted, which the cache ignored - and purgeGroup ran
//access_group_purge on it, which took the user's acl rows before its last delete failed.  An id or slug of the other kind now
//matches no row, so nothing changes and nothing is published.
#include "gtest/gtest.h"
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	struct IdentityKindTests : ::testing::Test{
		α SetUp()->void override{
			_user = UserPK{ GetId(GetUser("kindUser", GetRoot())) };
			_group = GroupPK{ GetId(GetGroup("kindGroup", GetRoot())) };
		}
		α TearDown()->void override{
			PurgeUser( _user, GetRoot() );
			PurgeGroup( _group, GetRoot() );
		}
		Ω mutate( string ql )ε->void{ QL().QuerySync<jvalue>( "mutation "+ql, {}, GetRoot() ); }
		α user( sv cols )ε->jobject{ return Select( "user", _user.Value, GetRoot(), cols, true ); }
		α group( sv cols )ε->jobject{ return Select( "group", _group.Value, GetRoot(), cols, true ); }
		UserPK _user;
		GroupPK _group;
	};

	TEST_F( IdentityKindTests, DeleteAndRestoreStayInTheirKind ){
		let userSlug = string{ Json::AsString(user("id slug"), "slug") };
		let groupSlug = string{ Json::AsString(group("id slug"), "slug") };
		EXPECT_NO_THROW( mutate(Ƒ("deleteGroup( id:{} )", _user.Value)) );
		EXPECT_NO_THROW( mutate(Ƒ("deleteGroup( slug:\"{}\" )", userSlug)) );
		EXPECT_TRUE( user("id deleted").at("deleted").is_null() ) << "deleteGroup reached a user";
		EXPECT_NO_THROW( mutate(Ƒ("deleteUser( id:{} )", _group.Value)) );
		EXPECT_NO_THROW( mutate(Ƒ("deleteUser( slug:\"{}\" )", groupSlug)) );
		EXPECT_TRUE( group("id deleted").at("deleted").is_null() ) << "deleteUser reached a group";

		Delete( "user", _user.Value, GetRoot() );
		ASSERT_FALSE( user("id deleted").at("deleted").is_null() );
		EXPECT_NO_THROW( mutate(Ƒ("restoreGroup( id:{} )", _user.Value)) );
		EXPECT_FALSE( user("id deleted").at("deleted").is_null() ) << "restoreGroup reached a user";
		Restore( "user", _user.Value, GetRoot() );
	}

	TEST_F( IdentityKindTests, UpdateStaysInItsKind ){
		const string before{ Json::FindDefaultSV(user("id description"), "description") };
		EXPECT_NO_THROW( mutate(Ƒ("updateGroup( id:{}, description:\"set through groups\" )", _user.Value)) );
		EXPECT_EQ( Json::FindDefaultSV(user("id description"), "description"), before ) << "updateGroup reached a user";
		const string groupBefore{ Json::FindDefaultSV(group("id description"), "description") };
		EXPECT_NO_THROW( mutate(Ƒ("updateUser( id:{}, description:\"set through users\" )", _group.Value)) );
		EXPECT_EQ( Json::FindDefaultSV(group("id description"), "description"), groupBefore ) << "updateUser reached a group";
	}

	TEST_F( IdentityKindTests, PurgeStaysInItsKind ){
		let grant = CreateAcl( _user, ERights::Read, ERights::None, "groups", GetRoot() );
		EXPECT_NO_THROW( mutate(Ƒ("purgeGroup( id:{} )", _user.Value)) );
		EXPECT_FALSE( user("id").empty() ) << "purgeGroup reached a user";
		EXPECT_FALSE( SelectAcl(_user, "groups").empty() ) << "access_group_purge took a user's acl rows";
		EXPECT_NO_THROW( mutate(Ƒ("purgeUser( id:{} )", _group.Value)) );
		EXPECT_FALSE( group("id").empty() ) << "purgeUser reached a group";
		PurgeAcl( _user, grant, GetRoot() );
	}

	//...and the membership add, which TestAddGroupMember gates:  it checked the members for cycles but never that the parent was
	//a group, and access_groups' fk is to access_identities - so a user could be given members.
	TEST_F( IdentityKindTests, OnlyAGroupHasMembers ){
		EXPECT_THROW( mutate(Ƒ("addGroup( id:{}, memberId:[{}] )", _user.Value, _group.Value)), Exception );
		EXPECT_NO_THROW( mutate(Ƒ("addGroup( id:{}, memberId:[{}] )", _group.Value, _user.Value)) );
		EXPECT_NO_THROW( mutate(Ƒ("removeGroup( id:{}, memberId:[{}] )", _group.Value, _user.Value)) );
	}
}

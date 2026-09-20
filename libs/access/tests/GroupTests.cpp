#include "gtest/gtest.h"
#include <jde/ql/ql.h>
#include <jde/ql/types/Introspection.h>
#include <jde/ql/QLAwait.h>
#include <jde/ql/types/TableQL.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/fwk/str.h>
#include "globals.h"

#define let const auto
namespace Jde::Access::Tests{
	using namespace Json;
	class GroupTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()->void;
	};

	α GroupTests::SetUpTestCase()->void{
	}
	Ω IsMember( str slug, GroupPK child, UserPK executer )ε->bool{
		let group = SelectGroup( slug, executer, true );
		if( auto members = FindArray(group, "groupMembers"); members ){
			for( let& member : *members ){
				if( GetId(AsObject(member))==child.Value )
					return true;
			}
		}
		return false;
	}
	TEST_F( GroupTests, Fields ){
		//const QL::TableQL ql{ "" };
		let query = "{ __type(name: \"Group\") { fields { name type { name kind ofType{name kind ofType{name kind ofType{name kind}}} } } } }";
		let actual = QL().QuerySync( query, {}, GetRoot() );
		auto obj = Json::ReadJsonNet( Ƒ("{}/libs/access/config/access-ql.jsonnet", Process::GetEnv("JDE_DIR").value_or("./")) );
		QL::Introspection intro{ move(obj) };
		QL::RequestQL request = QL::Parse( query, {}, Schemas() );
		jobject expected = intro.Find("Group")->ToJson( request.Queries()[0].Tables[0] );
		ASSERT_EQ( serialize(actual), serialize(expected) );
	}

	TEST_F( GroupTests, Crud ){
		let group = GetGroup( "groupTest", GetRoot() );
		let id = GetId( group );

 		let update = Ƒ( "mutation updateGroup( \"id\":{}, \"name\":\"{}\" )", id, "newName" );
 		let updateJson = QL().QuerySync<jvalue>( update, {}, GetRoot() );
		ASSERT_TRUE( AsSV(SelectGroup("groupTest", GetRoot()), "name")=="newName" );

 		let del = Ƒ( "{{mutation deleteGroup(\"id\":{}) }}", id );
 		let deleteJson = QL().QuerySync<jvalue>( del, {}, GetRoot() );
		ASSERT_TRUE( SelectGroup("groupTest", GetRoot()).empty() );
		ASSERT_TRUE( !SelectGroup("groupTest", GetRoot(), true).empty() );

 		PurgeGroup( {id}, GetRoot() );
 		ASSERT_TRUE( SelectGroup("groupTest", GetRoot(), true).empty() );
	}
	//GroupAwait's id-only shortcut - meant for one group's members - also caught a list, so `groups{ id }` skipped the query
	//and answered {} whatever the table held:  a client counting groups read zero.
	TEST_F( GroupTests, ListOnlyIds ){
		let root = GetRoot();
		const GroupPK a{ GetId(GetGroup("listIdsA", root)) }, b{ GetId(GetGroup("listIdsB", root)) };
		let ids = QL().QuerySync<jvalue>( "groups{ id }", {}, root );
		ASSERT_TRUE( ids.is_array() ) << serialize( ids );
		flat_set<uint> found;
		for( let& row : ids.get_array() )
			found.emplace( GetId(AsObject(row)) );
		EXPECT_TRUE( found.contains(a.Value) && found.contains(b.Value) ) << serialize( ids );
		EXPECT_EQ( ids.get_array().size(), QL().QuerySync<jvalue>("groups{ id name }", {}, root).as_array().size() );
		PurgeGroup( a, root );
		PurgeGroup( b, root );
	}
	TEST_F( GroupTests, AddRemove ){
		let root = GetRoot();
		const GroupPK hrManagers{ GetId(GetGroup("HR-Managers", root)) };
		const UserPK manager{ GetId(GetUser("manager", root)) };
		RemoveFromGroup( hrManagers, {manager}, root );
		AddToGroup( hrManagers, {manager}, root );
		constexpr sv ql = "group(id:{}){{ groupMembers{{id name}} }}";
		ASSERT_EQ( Json::AsArray(QL().QuerySync( Ƒ(ql, hrManagers.Value), {}, root), "groupMembers").size(), 1 );

		const GroupPK hr{ GetId( GetGroup("HR", root) ) };
		const UserPK associate{ GetId( GetUser("associate", root) ) };
		RemoveFromGroup( hr, {hrManagers, associate}, root );
		AddToGroup( {hr}, {hrManagers, associate}, root );
		let members = QL().QuerySync( Ƒ(ql, hr.Value), {}, root );
		let array = Json::AsArrayPath( members, "groupMembers" );
		ASSERT_EQ( array.size(), 2 );

		constexpr sv userQL = "user(id:{}){{ groups{{id name}} }}";
		ASSERT_EQ( Json::AsArrayPath(QL().QuerySync(Ƒ(userQL, manager.Value), {}, root), "groups" ).size(), 1 );

		RemoveFromGroup( hr, {hrManagers, associate}, root );
		RemoveFromGroup( hrManagers, {manager}, root );
		PurgeGroup( hr, root );
		PurgeGroup( hrManagers, root );
		PurgeUser( associate, root );
		PurgeUser( manager, root );
	}

	//ql-review3 #13: QL::getChildParentParams accepts the map parent column's json name as well as "id", but GroupHook read
	//only "id", with AsNumber, inside a ι hook - and QL::MutationAwaits::await_ready is a second noexcept frame above it, so
	//`addGroup( identityId:N, memberId:[M] )` from anyone holding Update on groups terminated the server.
	TEST_F( GroupTests, AddByParentColumnName ){
		let root = GetRoot();
		const GroupPK group{ GetId(GetGroup("review13-group", root)) };
		const UserPK member{ GetId(GetUser("review13-member", root)) };
		RemoveFromGroup( group, {member}, root );

		EXPECT_NO_THROW( QL().QuerySync<jvalue>(Ƒ("mutation addGroup( identityId:{}, memberId:[{}] )", group.Value, member.Value), {}, root) );
		EXPECT_TRUE( IsMember("review13-group", GroupPK{member.Value}, root) ) << "identityId has to mean what id means";

		RemoveFromGroup( group, {member}, root );
		PurgeGroup( group, root );
		PurgeUser( member, root );
	}

	//and neither spelling present is an error the caller receives, not a noexcept frame going down with it.  The ql validator
	//gets there first (getChildParentParams runs ahead of the hook), so its message is the one that arrives;  GroupHook's own
	//guard is the backstop for a hook order that ever changes.
	TEST_F( GroupTests, AddWithNoParentIdThrows ){
		let root = GetRoot();
		const UserPK member{ GetId(GetUser("review13-orphan", root)) };
		try{
			QL().QuerySync<jvalue>( Ƒ("mutation addGroup( memberId:[{}] )", member.Value), {}, root );
			ADD_FAILURE() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("identityId"), string::npos ) << e.what(); //named, delivered, and not a terminate.
		}
		PurgeUser( member, root );
	}

	TEST_F( GroupTests, Recursion ){
		const GroupPK groupA{ GetId(GetGroup("groupA", GetRoot())) };
		const GroupPK groupB{ GetId(GetGroup("groupB", GetRoot())) };
		if( !IsMember( "groupA", groupB, GetRoot()) )
			AddToGroup( groupA, {groupB}, GetRoot() );
		const GroupPK groupC{ GetId(GetGroup("groupC", GetRoot())) };
		if( !IsMember( "groupB", groupC, GetRoot()) )
			AddToGroup( groupB, {groupC}, GetRoot() );

		const GroupPK groupD{ GetId(GetGroup("groupD", GetRoot())) };
		EXPECT_THROW( AddToGroup( groupD, {groupD}, GetRoot() ), Exception );
		if( !IsMember( "groupC", groupD, GetRoot()) )
			AddToGroup( groupC, {groupD}, GetRoot() );
		EXPECT_THROW( AddToGroup( groupD, {groupA}, GetRoot() ), Exception );
		//TODO test implement deleted members.
	}

	//access-review3 #8's other half:  the hook's key set had diverged from getChildParentParams' (`id` only against either), so a
	//fix that merely swallowed the throw would have skipped TestAddGroupMember for the `identityId` spelling and let a cycle in.
	//The ancestry check has to fire under either spelling.
	TEST_F( GroupTests, AncestryCheckFiresByParentColumnName ){
		let root = GetRoot();
		const GroupPK outer{ GetId(GetGroup("review8-outer", root)) };
		const GroupPK inner{ GetId(GetGroup("review8-inner", root)) };
		if( !IsMember("review8-outer", inner, root) )
			AddToGroup( outer, {inner}, root );
		EXPECT_THROW( QL().QuerySync<jvalue>(Ƒ("mutation addGroup( identityId:{}, memberId:[{}] )", inner.Value, outer.Value), {}, root), Exception ); //outer into inner - its own ancestor.
		EXPECT_FALSE( IsMember("review8-inner", outer, root) ) << "the cycle must not have been written";
		EXPECT_THROW( QL().QuerySync<jvalue>(Ƒ("mutation addGroup( identityId:{}, memberId:[{}] )", inner.Value, inner.Value), {}, root), Exception ); //itself.
		RemoveFromGroup( outer, {inner}, root );
		PurgeGroup( inner, root );
		PurgeGroup( outer, root );
	}

	//access-review3 #9 (ql-review3 #5):  GroupAwait::Select adds is_group through TableQL::AddFilter, whose as_object() on a
	//client-supplied `filter` arg was a terminate.  Refused as an unknown column now.
	TEST_F( GroupTests, ScalarFilterArgIsRefusedNotFatal ){
		EXPECT_THROW( QL().QuerySync<jarray>("groups( filter:\"x\" ){ id }", {}, GetRoot()), Exception );
	}

	//GroupAwait::Select redirects the list to identities, but the redirect used to rename only the json name and leave the
	//query's table on access_groups - the member map, one row per member - so SelectStatement joined it back to its identities
	//base and a two-member group came back as two identical rows on /access/groups.
	TEST_F( GroupTests, MultiMemberGroupIsOneListRow ){
		let root = GetRoot();
		const GroupPK group{ GetId(GetGroup("list-fanout", root)) };
		const UserPK first{ GetId(GetUser("list-fanout-a", root)) };
		const UserPK second{ GetId(GetUser("list-fanout-b", root)) };
		AddToGroup( group, {first, second}, root );

		let rows = QL().QuerySync<jarray>( "groups( limit:100 ){ id name slug }", {}, root );
		uint count{};
		for( let& row : rows )
			count += GetId( AsObject(row) )==group.Value;
		EXPECT_EQ( count, 1u ) << serialize( rows );

		RemoveFromGroup( group, {first, second}, root );
		PurgeGroup( group, root );
		PurgeUser( first, root );
		PurgeUser( second, root );
	}
}
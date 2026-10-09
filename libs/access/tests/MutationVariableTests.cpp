//ql-review3 #23: the stock update/add/remove paths read _mutation.Args raw, and the parser leaves a $variable there as the
//marker string "\b$name" - only Input::FindPtr/As*/ExtrapolateVariables translate it.  So a variable was persisted verbatim,
//a flags column was wiped to 0 (the marker is neither an array nor a number), and an `id:$id` update fell through to the
//natural-key branch and updated whichever row shared that name.  Every one of these was reported as success.
#include "gtest/gtest.h"
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	Ω ql( str text )ε->string{ return Str::Replace( Str::Replace(text, '<', '{'), '>', '}' ); }//<> for {}: the mutation text is built by hand.

	TEST( MutationVariableTests, UpdateNameFromAVariable ){
		let root = GetRoot();
		let group = GroupPK{ GetId(GetGroup("review23-group", root)) };
		QL().QuerySync<jvalue>( "mutation updateGroup( id:"+std::to_string(group.Value)+", name:$n )", jobject{{"n","review23-renamed"}}, root );
		EXPECT_EQ( Json::AsSV(Select("group", group.Value, root, "id name", true), "name"), "review23-renamed" );//not the "\b$n" marker.
		PurgeGroup( group, root );
	}

	//the key half:  with the id unresolved the where clause fell back to `name=...`, so the update either hit the wrong row or
	//silently hit none - and reported success either way.
	TEST( MutationVariableTests, UpdateKeyedByAVariableId ){
		let root = GetRoot();
		let group = GroupPK{ GetId(GetGroup("review23-keyed", root)) };
		QL().QuerySync<jvalue>( "mutation updateGroup( id:$id, description:$d )", jobject{{"id",group.Value},{"d","review23-desc"}}, root );
		EXPECT_EQ( Json::AsSV(Select("group", group.Value, root, "id description", true), "description"), "review23-desc" );
		PurgeGroup( group, root );
	}

	//flags:  the marker is neither an array nor a number, so the branch fell through with value=0 and wrote the wipe.
	TEST( MutationVariableTests, UpdateFlagsFromAVariable ){
		let root = GetRoot();
		let resource = SelectResource( "groups", root, true );
		let before = Json::AsArray( resource, "allowed" );
		ASSERT_FALSE( before.empty() ) << "the fixture needs a resource that already has rights";

		const UserPK system{ UserPK::System };//a resource write by anyone else needs Administer on every enforced root of its schema - this is about variables.
		QL().QuerySync<jvalue>( "mutation updateResource( id:"+std::to_string(GetId(resource))+", allowed:$a )", jobject{{"a",jarray{"Read"}}}, system );
		let after = Json::AsArray( SelectResource("groups", root, true), "allowed" );
		ASSERT_EQ( after.size(), 1u ) << "the flags column was wiped: " << serialize( after );
		EXPECT_EQ( after[0].as_string(), "Read" );

		//put it back the way the suite found it.
		string names; for( let& v : before ) names += (names.empty() ? "\"" : ",\"")+string{v.as_string()}+"\"";
		QL().QuerySync<jvalue>( "mutation updateResource( id:"+std::to_string(GetId(resource))+", allowed:["+names+"] )", {}, system );
	}

	//add/remove:  getChildParentParams read the raw args, so both ids arrived as marker strings.
	TEST( MutationVariableTests, AddGroupMemberFromVariables ){
		let root = GetRoot();
		let group = GroupPK{ GetId(GetGroup("review23-add", root)) };
		let member = UserPK{ GetId(GetUser("review23-member", root)) };
		jobject vars{ {"g",group.Value}, {"m",member.Value} };
		EXPECT_NO_THROW( QL().QuerySync<jvalue>("mutation addGroup( id:$g, memberId:$m )", vars, root) );

		let members = Json::AsArrayPath( QL().QuerySync(ql("group(id:"+std::to_string(group.Value)+")<groupMembers<id>>"), {}, root), "groupMembers" );
		ASSERT_EQ( members.size(), 1u );
		EXPECT_EQ( Json::AsNumber<uint>(Json::AsObject(members[0]), "id"), member.Value );

		RemoveFromGroup( group, {IdentityPK{member}}, root );
		PurgeGroup( group, root );
		PurgeUser( member, root );
	}

	TEST( MutationVariableTests, ATypoInAQueryFilterIsAnErrorNotZeroRows ){
		let root = GetRoot();
		const string slug{ "review36-query" };
		let user = UserPK{ GetId(GetUser(slug, root)) };

		let control = QL().QuerySync<jarray>( "users( slug:$slug ){ id slug }", jobject{{"slug",slug}}, root );
		EXPECT_EQ( control.size(), 1u ) << "the fixture query itself found nothing"; //the same query, spelled right.
		try{
			QL().QuerySync<jarray>( "users( slug:$targt ){ id slug }", jobject{{"slug",slug}}, root );
			ADD_FAILURE() << "the typo returned rows instead of an error";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("targt"), string::npos ) << e.what();  //which variable.
			EXPECT_NE( string{e.what()}.find("slug"), string::npos ) << e.what(); //and what was bound.
		}
		PurgeUser( user, root );
	}
	TEST( MutationVariableTests, ATypoInAMutationArgIsAnErrorNotANullWrite ){
		let root = GetRoot();
		let group = GroupPK{ GetId(GetGroup("review36-group", root)) };
		try{
			QL().QuerySync<jvalue>( "mutation updateGroup( id:"+std::to_string(group.Value)+", name:$nme )", jobject{{"name","review36-renamed"}}, root );
			ADD_FAILURE() << "the typo was accepted";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("nme"), string::npos ) << e.what();
		}
		//AsSV throws on a null, so this is the write assertion as much as the value one.
		EXPECT_EQ( Json::AsSV(Select("group", group.Value, root, "id name", true), "name"), "review36-group - name" );//GetGroup's own spelling, untouched.
		PurgeGroup( group, root );
	}

	//ql-review3 #49: AddRemoveAwait::Add's `input:{…}` appended a `?` per extra value to a column list fixed at the two mapped
	//columns, so every use of the feature emitted `insert into t(a,b)values(?,?,?)` - `3 values for 2 columns`, regardless of
	//which column was asked for.  The statement is well formed now, which is what this asserts:  the failure moved from the
	//generator (a count) to the schema (a name).  access has no map table with a third column - the shape the feature exists for
	//is the log schema's entryArgMap.arg_index - so the success path is not reachable from any suite; `created` is a column of
	//identities, which access_groups extends but does not carry, and Table::GetColumnPtr resolves it through Extends.
	TEST( MutationVariableTests, AddWithExtraInputColumnsNamesThemInTheStatement ){
		let root = GetRoot();
		let group = GroupPK{ GetId(GetGroup("review49-group", root)) };
		let member = UserPK{ GetId(GetUser("review49-member", root)) };
		let m = Ƒ( "mutation addGroup( id:{}, memberId:{}, input:{{ created:\"2026-01-01T00:00:00\" }} )", group.Value, member.Value );
		try{
			QL().QuerySync<jvalue>( m, {}, root );
			ADD_FAILURE() << "access_groups has no `created` column - this cannot succeed";
		}
		catch( const Exception& e ){
			let what = string{ e.what() };
			EXPECT_EQ( what.find("values for"), string::npos ) << "still a column-count error: " << what;//`3 values for 2 columns`.
			EXPECT_NE( what.find("created"), string::npos ) << what;//the column is named in the statement now, so the db can say which.
		}
		//nothing was added:  the statement never ran.
		let members = Json::AsArrayPath( QL().QuerySync(ql("group(id:"+std::to_string(group.Value)+")<groupMembers<id>>"), {}, root), "groupMembers" );
		EXPECT_TRUE( members.empty() );
		PurgeGroup( group, root );
		PurgeUser( member, root );
	}
	//the control: an add with no `input:` is the shape everything actually sends, and it still works.
	TEST( MutationVariableTests, AddWithoutExtraInputColumnsStillWorks ){
		let root = GetRoot();
		let group = GroupPK{ GetId(GetGroup("review49-plain", root)) };
		let member = UserPK{ GetId(GetUser("review49-plainMember", root)) };
		QL().QuerySync<jvalue>( Ƒ("mutation addGroup( id:{}, memberId:{} )", group.Value, member.Value), {}, root );
		let members = Json::AsArrayPath( QL().QuerySync(ql("group(id:"+std::to_string(group.Value)+")<groupMembers<id>>"), {}, root), "groupMembers" );
		ASSERT_EQ( members.size(), 1u );
		RemoveFromGroup( group, {IdentityPK{member}}, root );
		PurgeGroup( group, root );
		PurgeUser( member, root );
	}

	//ql-review3 #54: the caller's own reply is shaped by the same arg-wins merge as the subscription payload (QLAwait), so a
	//`createUser( id:99, … )` was answered with 99 rather than the id the database assigned - the client's row would be
	//unreachable by the id it was just given.  The pk is not insertable, so 99 was never written anywhere; only the reply lied.
	TEST( MutationVariableTests, AClientSuppliedIdDoesNotMaskTheAssignedOne ){
		let root = GetRoot();
		const string slug{ "review54-user" };
		let y = QL().QuerySync<jobject>( "mutation createUser( name:\""+slug+"\", slug:\""+slug+"\", providerId:1, id:99 ){ id }", {}, root );
		let id = GetId( y );
		EXPECT_NE( id, 99u ) << serialize( y );
		EXPECT_EQ( GetId(Select("user", id, root, "id slug", true)), id );//and the id it answered with finds the row.
		PurgeUser( UserPK{id}, root );
	}
}

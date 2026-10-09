//ql-review3 #46: `updateable` defaults to true and none of common-meta's sequenced pk shapes says otherwise, so CreateUpdate's
//only filter - `if( !c->Updateable ) continue;` - let a client set the primary key: `updateResource( id:6, resourceId:5006 )`
//emitted `update access_resources set resource_id = 5006 … where access_resources.resource_id=6` and renumbered the row, while
//Authorize's in-memory Resources map still held 6.  The actor needs Update on the table already, so the harm is referential and
//cache corruption rather than new privilege.  A key column is simply not settable now, the same way a non-updateable one is not.
#include "gtest/gtest.h"
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	//the write-up's own case, on the table it used.
	TEST( KeyColumnUpdateTests, APkColumnCannotBeSetThroughTheGenericUpdate ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };//the row it creates is an enforced root nobody administers, so only the system may update it - the refusal below has to be the column's.
		const string slug{ "review46-resource" };
		let id = Create( "resource", slug, system, "schemaName:\"review46\"" );

		//and the request is refused rather than reported as a success that did nothing:  with the pk skipped there is no settable
		//column left, which UpdateAwait already has a message for.
		try{
			QL().QuerySync<jvalue>( Ƒ("mutation updateResource( id:{}, resourceId:{} )", id, id+5000), {}, UserPK{UserPK::System} );
			ADD_FAILURE() << "the renumber was accepted";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("nothing to update"), string::npos ) << e.what();
		}
		EXPECT_EQ( GetId(Select("resource", id, root, "id slug", true)), id ) << "the row was renumbered";
		EXPECT_TRUE( Select("resource", id+5000, root, "id", true).empty() ) << "and it is not at the new number either";

		Purge( "resource", id, system );
	}

	//the extension chain: the parent statement is built first, so `identityId` reached `update access_identities set
	//identity_id = ?` before anything downstream could object.  Only a child row and sqlite's FKs stopped it, and an
	//identity-only shell has no child.
	TEST( KeyColumnUpdateTests, APkColumnCannotBeSetThroughAnExtendedTablesParent ){
		let root = GetRoot();
		const string slug{ "review46-user" };
		let user = UserPK{ GetId(GetUser(slug, root)) };

		QL().QuerySync<jvalue>( Ƒ("mutation updateUser( id:{}, identityId:{}, loginName:\"review46-login\" )", user.Value, user.Value+7000), {}, root );
		let after = Select( "user", user.Value, root, "id loginName", true );
		EXPECT_EQ( GetId(after), user.Value ) << "the identities row was renumbered";
		EXPECT_EQ( Json::AsSV(after, "loginName"), "review46-login" );//the rest of the mutation still applied.

		PurgeUser( user, root );
	}

	//the control:  an ordinary column of the same table is still settable, which is the whole point of the statement.
	TEST( KeyColumnUpdateTests, OrdinaryColumnsAreStillUpdateable ){
		let root = GetRoot();
		const string slug{ "review46-plain" };
		let id = Create( "resource", slug, UserPK{UserPK::System}, "schemaName:\"review46\"" );

		QL().QuerySync<jvalue>( Ƒ("mutation updateResource( id:{}, description:\"review46-desc\" )", id), {}, UserPK{UserPK::System} );
		EXPECT_EQ( Json::AsSV(Select("resource", id, root, "id description", true), "description"), "review46-desc" );

		Purge( "resource", id, UserPK{UserPK::System} );
	}
}

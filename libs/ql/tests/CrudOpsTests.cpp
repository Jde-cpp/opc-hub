//#238:  the stock crud ops end to end - select, insert, update, delete/restore, purge - through LocalQL against the qlTests
//schema on sqlite, with a LocalSubscriptions listener on each mutation's event.  Until now these ran only in Jde.Access.Tests,
//through access's own schema, hooks and authorizer;  here nothing stands between the document and the op.  Each test owns the
//rows it makes (`crud-…` slugs) and purges them in TearDown, so the suite stays green under --gtest_repeat/--gtest_shuffle.
#include <gtest/gtest.h>
#include <jde/db/DBException.h>
#include "globals.h"

#define let const auto

namespace Jde::QL::Tests{
	using namespace Json;

	struct CrudOpsTests : OpsFixture{
		Ω Select( uint id, sv columns="id name slug", bool includeDeleted=false )ε->jobject{
			return QueryObject( Ƒ("item( id:{}{} ){{ {} }}", id, includeDeleted ? "" : ", deleted:null", columns) );
		}
	};

	//insert:  the new id comes back through the result request, the row is what was sent plus the server's defaults, and the
	//subscriber hears it with the id - the insert twin's out row, so no id lookup is involved here.
	TEST_F( CrudOpsTests, CreateReturnsTheIdAndNotifies ){
		auto listener = Listen( "subscription ItemCreated{ itemCreated(subscriptionId:$id){ id name slug } }" );
		let id = Item( "crud-create", Gizmo, R"(, description:"d", attributes:3)" );
		EXPECT_GT( id, 0u );

		let row = Select( id, "id name slug description attributes itemType created updated deleted" );
		EXPECT_EQ( AsNumber<uint>(row, "id"), id );
		EXPECT_EQ( AsSV(row, "name"), "crud-create" );
		EXPECT_EQ( AsSV(row, "slug"), "crud-create" );
		EXPECT_EQ( AsSV(row, "description"), "d" );
		EXPECT_EQ( AsNumber<uint>(row, "attributes"), 3u );
		EXPECT_EQ( AsSV(row, "itemType"), Gizmo );//rendered through the enum table, not as the id.
		EXPECT_TRUE( AsSV(row, "created").ends_with('Z') ) << serialize( row );//$now, as an iso string.
		EXPECT_TRUE( row.at("updated").is_null() );
		EXPECT_TRUE( row.at("deleted").is_null() );

		ASSERT_EQ( listener->Changes.size(), 1u );
		EXPECT_EQ( AsNumber<uint>(listener->Body(0), "id"), id );
		EXPECT_EQ( AsSV(listener->Body(0), "name"), "crud-create" );
		EXPECT_FALSE( listener->Body(0).contains("description") );//trimmed to what was subscribed.
	}
	//the nested-key spelling:  `itemType:{ id:2 }` names the fk's row.  Its member is the enum's singular, so the object is the
	//column's own value (JsonColumn::ToValue), on insert and update alike.
	TEST_F( CrudOpsTests, ANestedKeyNamesTheEnumRow ){
		let id = Track( Create(R"(mutation createItem( name:"crud-nested", slug:"crud-nested", itemType:{ id:2 } ){ id })") );
		EXPECT_EQ( AsSV(Select(id, "id itemType"), "itemType"), Gizmo );
		Query( Ƒ("mutation updateItem( id:{}, itemType:{{ id:1 }} )", id) );
		EXPECT_EQ( AsSV(Select(id, "id itemType"), "itemType"), Gadget );
		EXPECT_NE( WhatOf(Ƒ(R"(mutation updateItem( id:{}, itemType:{{ id:2, name:"Widget" }} ))", id)).find("not a row to create"), string::npos );
		EXPECT_EQ( AsSV(Select(id, "id itemType"), "itemType"), Gadget );
	}
	//the refusals - ql's before anything is written, the database's by its constraints - and none reaches a subscriber (#47).
	TEST_F( CrudOpsTests, CreateRefusals ){
		auto listener = Listen( "subscription ItemCreated{ itemCreated(subscriptionId:$id){ id } }" );
		EXPECT_NE( WhatOf(R"(mutation createItem( slug:"crud-noname", itemType:"Gadget" ){ id })").find("No default for 'name'"), string::npos );//not nullable, no default.
		EXPECT_NE( WhatOf(R"(mutation createItem( name:"crud-noenum", slug:"crud-noenum", itemType:"Doohickey" ){ id })").find("Doohickey"), string::npos );//no such enum name.
		//an enum's object is a key, not a row to create - nothing reads the new enum row's id back, so the item would bind to 0.
		EXPECT_NE( WhatOf(R"(mutation createItem( name:"crud-nowidget", slug:"crud-nowidget", itemType:{ id:3, name:"Widget" } ){ id })").find("not a row to create"), string::npos );
		EXPECT_EQ( Count("item_types", "where item_type_id=3"), 0u ) << "the nested enum row was written";
		EXPECT_NE( WhatOf(R"(mutation createItem( name:"crud-nodesc", slug:"crud-nodesc", itemType:"Gadget", description:{ x:1 } ){ id })").find("not an object"), string::npos );
		//a column the meta marks insertable:false is the server's to set:  refused, where it was dropped without a word (servers-0.2 #11).
		EXPECT_NE( WhatOf(R"(mutation createItem( name:"crud-nocreated", slug:"crud-nocreated", itemType:"Gadget", created:"2020-01-01T00:00:00Z" ){ id })").find("isn't insertable"), string::npos );
		try{
			Query( R"(mutation createItem( name:"crud-nofk", slug:"crud-nofk", itemType:99 ){ id })" );//an id the enum table lacks - the fk's, not ql's.
			ADD_FAILURE() << "the foreign key was not enforced";
		}
		catch( const DB::DBException& e ){
			EXPECT_EQ( e.Error, DB::EDbError::ForeignKey ) << e.what();
		}
		let id = Item( "crud-dup" );
		try{
			Query( R"(mutation createItem( name:"crud-dup", slug:"crud-dup-2", itemType:"Gadget" ){ id })" );//name is a natural key.
			ADD_FAILURE() << "the natural key was not enforced";
		}
		catch( const DB::DBException& e ){
			EXPECT_EQ( e.Error, DB::EDbError::Duplicate ) << e.what();
		}
		vector<DB::Value> refused;//by name:  a pattern would also match other tests' rows, which a crashed run on a file db leaves behind.
		for( sv slug : {"crud-noname", "crud-noenum", "crud-nowidget", "crud-nodesc", "crud-nocreated", "crud-nofk", "crud-dup-2"} )
			refused.emplace_back( string{slug} );
		EXPECT_EQ( Count("items", "where slug in (?,?,?,?,?,?,?)", move(refused)), 0u ) << "a refused insert was written";
		ASSERT_EQ( listener->Changes.size(), 1u ) << "a refused insert was published";//crud-dup's own, and no other.
		EXPECT_EQ( AsNumber<uint>(listener->Body(0), "id"), id );
	}

	//update:  the columns sent are set and `updated` is stamped by the statement;  the subscriber hears the id - sent, or
	//recovered from the natural key (findId) when the mutation was keyed by slug.
	TEST_F( CrudOpsTests, UpdateSetsColumnsAndNotifies ){
		let id = Item( "crud-update" );
		auto listener = Listen( "subscription ItemUpdated{ itemUpdated(subscriptionId:$id){ id description } }" );

		Query( Ƒ(R"(mutation updateItem( id:{}, description:"by id", itemType:2 ))", id) );
		let row = Select( id, "id description itemType updated" );
		EXPECT_EQ( AsSV(row, "description"), "by id" );
		EXPECT_EQ( AsSV(row, "itemType"), Gizmo );
		EXPECT_FALSE( row.at("updated").is_null() ) << "`updated` was not stamped";

		Query( R"(mutation updateItem( slug:"crud-update", description:"by slug" ))" );
		EXPECT_EQ( AsSV(Select(id, "id description"), "description"), "by slug" );

		ASSERT_EQ( listener->Changes.size(), 2u );
		EXPECT_EQ( AsNumber<uint>(listener->Body(0), "id"), id );
		EXPECT_EQ( AsSV(listener->Body(0), "description"), "by id" );
		EXPECT_EQ( FindNumber<uint>(listener->Body(1), "id"), optional{id} ) << "keyed by slug - the id has to be looked up";
		EXPECT_EQ( AsSV(listener->Body(1), "description"), "by slug" );
	}
	//#46: a key column is not a settable one - with nothing left to set the update is refused rather than run empty.  An
	//updateable:false column a caller names is refused for itself (servers-0.2 #11), where it was dropped without a word.
	TEST_F( CrudOpsTests, UpdateOfNothingSettableIsRefused ){
		let id = Item( "crud-nothing" );
		EXPECT_NE( WhatOf(Ƒ("mutation updateItem( id:{} )", id)).find("nothing to update"), string::npos );
		EXPECT_NE( WhatOf(Ƒ("mutation updateItem( id:{}, itemId:999 )", id)).find("nothing to update"), string::npos );
		EXPECT_NE( WhatOf(Ƒ(R"(mutation updateItem( id:{}, created:"2020-01-01T00:00:00Z" ))", id)).find("isn't updateable"), string::npos );
		EXPECT_NE( WhatOf(Ƒ(R"(mutation updateItem( id:{}, description:"set", created:"2020-01-01T00:00:00Z" ))", id)).find("isn't updateable"), string::npos ) << "refused whole, not set without it";
		EXPECT_TRUE( Select(id, "id description").at("description").is_null() );
		EXPECT_EQ( AsNumber<uint>(Select(id), "id"), id );
	}

	//delete and restore are updates of `deleted`:  a deleted row drops out of a `deleted:null` select and stays for one without,
	//and each publishes under its own event with the id - recovered by the lookup when the mutation was keyed by slug.
	TEST_F( CrudOpsTests, DeleteAndRestore ){
		let id = Item( "crud-delete" );
		auto deleted = Listen( "subscription ItemDeleted{ itemDeleted(subscriptionId:$id){ id } }" );
		auto restored = Listen( "subscription ItemRestored{ itemRestored(subscriptionId:$id){ id } }" );

		Query( Ƒ("mutation deleteItem( id:{} )", id) );
		EXPECT_TRUE( Select(id).empty() ) << "a deleted row answered a live select";
		let gone = Select( id, "id deleted", true );
		EXPECT_EQ( AsNumber<uint>(gone, "id"), id );
		EXPECT_TRUE( AsSV(gone, "deleted").ends_with('Z') ) << serialize( gone );

		Query( R"(mutation restoreItem( slug:"crud-delete" ))" );
		EXPECT_EQ( AsNumber<uint>(Select(id), "id"), id );
		EXPECT_TRUE( Select(id, "id deleted", true).at("deleted").is_null() );

		ASSERT_EQ( deleted->Changes.size(), 1u );
		EXPECT_EQ( AsNumber<uint>(deleted->Body(0), "id"), id );
		ASSERT_EQ( restored->Changes.size(), 1u );
		EXPECT_EQ( FindNumber<uint>(restored->Body(0), "id"), optional{id} ) << "keyed by slug - the id has to be looked up";
	}
	//#47:  a statement that matched nothing is not an event.  The extra arg ands into the where clause, so the row is untouched
	//and nobody is told otherwise;  a purge of a row that is not there is the same silence.
	TEST_F( CrudOpsTests, AMutationThatMatchesNothingDoesNotNotify ){
		let id = Item( "crud-nomatch" );
		auto deleted = Listen( "subscription ItemDeleted{ itemDeleted(subscriptionId:$id){ id } }" );
		auto purged = Listen( "subscription ItemPurged{ itemPurged(subscriptionId:$id){ id } }" );
		Query( Ƒ(R"(mutation deleteItem( id:{}, name:"nomatch" ))", id) );
		Query( "mutation purgeItem( id:987654321 )" );
		EXPECT_EQ( AsNumber<uint>(Select(id), "id"), id ) << "the row was touched";
		EXPECT_TRUE( deleted->Changes.empty() ) << "a 0-row delete was published";
		EXPECT_TRUE( purged->Changes.empty() ) << "a 0-row purge was published";
	}

	//purge:  the row itself goes, deleted or not, and the subscriber hears the id.
	TEST_F( CrudOpsTests, PurgeRemovesTheRowAndNotifies ){
		let id = Item( "crud-purge" );
		auto listener = Listen( "subscription ItemPurged{ itemPurged(subscriptionId:$id){ id } }" );
		Query( Ƒ("mutation deleteItem( id:{} )", id) );
		Query( Ƒ("mutation purgeItem( id:{} )", id) );
		EXPECT_TRUE( Select(id, "id", true).empty() );
		EXPECT_EQ( Count("items", "where item_id=?", {DB::Value{id}}), 0u );
		ASSERT_EQ( listener->Changes.size(), 1u );
		EXPECT_EQ( AsNumber<uint>(listener->Body(0), "id"), id );
	}

	//select:  the shapes a client sends - plural and singular, a page, a pattern, an array, an enum by name, the live rows, and a
	//column that is not there.
	TEST_F( CrudOpsTests, SelectShapes ){
		for( let& [slug,type] : vector<std::pair<sv,sv>>{ {"crud-select-a",Gadget}, {"crud-select-b",Gizmo}, {"crud-select-c",Gadget} } )
			Item( slug, type );
		let slugs = []( str ql ){ vector<string> y; for( let& row : QueryArray(ql) ) y.emplace_back( AsSV(row.get_object(), "slug") ); return y; };
		EXPECT_EQ( slugs(R"(items( slug:{glob:"crud-select-*"}, orderBy:"slug" ){ id slug })"), (vector<string>{"crud-select-a","crud-select-b","crud-select-c"}) );
		EXPECT_EQ( slugs(R"(items( slug:{glob:"crud-select-*"}, orderBy:"slug", limit:1, offset:1 ){ id slug })"), (vector<string>{"crud-select-b"}) );
		EXPECT_EQ( slugs(R"(items( slug:{glob:"crud-select-*"}, itemType:"Gizmo", deleted:null ){ id slug })"), (vector<string>{"crud-select-b"}) );
		EXPECT_EQ( slugs(R"(items( slug:["crud-select-a","crud-select-c"], orderBy:"slug" ){ id slug })"), (vector<string>{"crud-select-a","crud-select-c"}) );
		EXPECT_TRUE( slugs(R"(items( slug:{glob:"crud-select-z*"} ){ id slug })").empty() );
		EXPECT_TRUE( QueryObject(R"(item( slug:"crud-select-none" ){ id slug })").empty() );//singular with no row:  an empty object, not null.
		EXPECT_EQ( AsSV(QueryObject(R"(item( slug:"crud-select-b" ){ id slug itemType })"), "itemType"), Gizmo );
		try{
			QueryArray( "items{ id bogus }" );
			ADD_FAILURE() << "an unknown column was selected";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("bogus"), string::npos ) << e.what();
		}
	}
	//a mutation's arguments through variables - `$slug` bound by the document's variables, as the SPA sends every mutation.
	TEST_F( CrudOpsTests, VariablesBindMutationArguments ){
		let id = Track( Create("mutation createItem( name:$slug, slug:$slug, itemType:$type ){ id }", jobject{{"slug","crud-vars"},{"type",Gadget}}) );
		EXPECT_EQ( AsSV(Select(id, "id slug itemType"), "itemType"), Gadget );
		Query( "mutation updateItem( id:$id, description:$d )", jobject{{"id",id},{"d","vars"}} );
		EXPECT_EQ( AsSV(Select(id, "id description"), "description"), "vars" );
	}
}
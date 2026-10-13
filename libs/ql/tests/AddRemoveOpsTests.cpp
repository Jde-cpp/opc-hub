//#238:  add/remove through a map table - item_labels, keyed (item_id, label_id), with no add/remove proc, so AddRemoveAwait's
//own statements run between the hooks - and the parent's nested select that reads the membership back through the map
//(SelectAwait::findMap).  With them the purge the map's fk refuses.  Rows are this suite's (`map-…`), purged in TearDown.
#include <gtest/gtest.h>
#include <jde/db/DBException.h>
#include <jde/db/IDataSource.h>
#include <jde/ql/LocalSubscriptions.h>
#include "globals.h"
#include "RecordingListener.h"

#define let const auto

namespace Jde::QL::Tests{
	using namespace Json;

	struct AddRemoveOpsTests : ::testing::Test{
		α SetUp()->void override{ EnsureItemTypes(); }
		α TearDown()->void override{
			for( auto& listener : _listeners )
				Subscriptions::StopListen( listener );
			for( let id : _items ){
				DS().ExecuteSync( {"delete from qlt_item_labels where item_id=?", {DB::Value{id}}} );
				DS().ExecuteSync( {"delete from qlt_items where item_id=?", {DB::Value{id}}} );
			}
			for( let id : _labels )
				DS().ExecuteSync( {"delete from qlt_labels where label_id=?", {DB::Value{id}}} );
		}
		α Item( sv slug )ε->uint{ return _items.emplace_back( Create(Ƒ(R"(mutation createItem( name:"{0}", slug:"{0}", itemType:"{1}" ){{ id }})", slug, Gadget)) ); }
		α Label( sv name )ε->uint{ return _labels.emplace_back( Create(Ƒ(R"(mutation createLabel( name:"{}", color:"red" ){{ id }})", name)) ); }
		α Listen( sv text )ε->sp<RecordingListener>{
			auto listener = _listeners.emplace_back( ms<RecordingListener>("AddRemoveOpsTests") );
			BlockTAwait<vector<SubscriptionId>>( move(*QLPtr()->Subscribe(string{text}, jobject{{"id",7717}}, listener, System)) );
			return listener;
		}
		//the membership as a client reads it:  the parent row with its labels nested, through the map.
		Ω Labels( uint item )ε->vector<string>{
			vector<string> y;
			for( let& label : AsArray(QueryObject(Ƒ("item( id:{} ){{ id labels{{ id name }} }}", item)), "labels") )
				y.emplace_back( AsSV(label.get_object(), "name") );
			std::ranges::sort( y );
			return y;
		}
		Ω WhatOf( string ql )ι->string{ //the refusal's message, or "" when nothing was refused.
			try{ Query( move(ql) ); }
			catch( const Exception& e ){ return e.what(); }
			return {};
		}
	private:
		vector<uint> _items, _labels;
		vector<sp<RecordingListener>> _listeners;
	};

	//several children in one add, the parent by `id` or by its column name, and each op's event with what the mutation said.
	TEST_F( AddRemoveOpsTests, AddAndRemoveThroughTheMap ){
		let item = Item( "map-add" );
		let red = Label( "map-add-red" );
		let blue = Label( "map-add-blue" );
		auto added = Listen( "subscription ItemLabelAdded{ itemLabelAdded(subscriptionId:$id){ id labelId } }" );
		auto removed = Listen( "subscription ItemLabelRemoved{ itemLabelRemoved(subscriptionId:$id){ id labelId } }" );

		Query( Ƒ("mutation addItemLabel( id:{}, labelId:[{},{}] )", item, red, blue) );
		EXPECT_EQ( Labels(item), (vector<string>{"map-add-blue","map-add-red"}) );
		EXPECT_EQ( Count("item_labels", "where item_id=?", {DB::Value{item}}), 2u );

		Query( Ƒ("mutation removeItemLabel( itemId:{}, labelId:{} )", item, red) );
		EXPECT_EQ( Labels(item), (vector<string>{"map-add-blue"}) );
		EXPECT_EQ( Count("item_labels", "where item_id=?", {DB::Value{item}}), 1u );

		ASSERT_EQ( added->Changes.size(), 1u );
		EXPECT_EQ( AsNumber<uint>(added->Body(0), "id"), item );
		EXPECT_EQ( AsArray(added->Body(0), "labelId").size(), 2u );
		ASSERT_EQ( removed->Changes.size(), 1u );
		EXPECT_EQ( AsNumber<uint>(removed->Body(0), "labelId"), red );
	}
	//the refusals - ql's for a mutation missing a side or on a table with no map, the database's for the map's key and fk - and
	//none of them reaches a subscriber (#47).
	TEST_F( AddRemoveOpsTests, AddRefusals ){
		let item = Item( "map-refuse" );
		let label = Label( "map-refuse-label" );
		auto added = Listen( "subscription ItemLabelAdded{ itemLabelAdded(subscriptionId:$id){ id } }" );
		EXPECT_NE( WhatOf(Ƒ("mutation addItemLabel( id:{} )", item)).find("labelId"), string::npos );//no child.
		EXPECT_NE( WhatOf(Ƒ("mutation addItemLabel( labelId:{} )", label)).find("itemId"), string::npos );//no parent.
		EXPECT_NE( WhatOf(Ƒ("mutation addItem( id:{}, labelId:{} )", item, label)).find("does not support add/remove"), string::npos );//items has no map.
		Query( Ƒ("mutation addItemLabel( id:{}, labelId:{} )", item, label) );
		try{
			Query( Ƒ("mutation addItemLabel( id:{}, labelId:{} )", item, label) );//(item_id, label_id) is the map's key.
			ADD_FAILURE() << "a duplicate membership was accepted";
		}
		catch( const DB::DBException& e ){
			EXPECT_EQ( e.Error, DB::EDbError::Duplicate ) << e.what();
		}
		try{
			Query( Ƒ("mutation addItemLabel( id:{}, labelId:987654321 )", item) );//a child that does not exist - the fk's.
			ADD_FAILURE() << "the foreign key was not enforced";
		}
		catch( const DB::DBException& e ){
			EXPECT_EQ( e.Error, DB::EDbError::ForeignKey ) << e.what();
		}
		EXPECT_EQ( Labels(item), (vector<string>{"map-refuse-label"}) );
		EXPECT_EQ( added->Changes.size(), 1u ) << "a refused add was published";
	}
	//#47 on the map:  removing what is not there changes nothing and tells nobody.
	TEST_F( AddRemoveOpsTests, RemovingAnAbsentChildIsNotAnEvent ){
		let item = Item( "map-absent" );
		let label = Label( "map-absent-label" );
		auto removed = Listen( "subscription ItemLabelRemoved{ itemLabelRemoved(subscriptionId:$id){ id } }" );
		Query( Ƒ("mutation removeItemLabel( id:{}, labelId:{} )", item, label) );
		EXPECT_TRUE( removed->Changes.empty() );
		EXPECT_TRUE( Labels(item).empty() );
	}
	//a purge is `delete from items where item_id=?`, and the map's fk has no cascade - a parent with children is refused by the
	//database, in place, until they are removed.  (The access tables with children purge through a proc for this reason.)
	TEST_F( AddRemoveOpsTests, APurgeWithChildrenIsRefusedByTheForeignKey ){
		let item = Item( "map-purge" );
		let label = Label( "map-purge-label" );
		auto purged = Listen( "subscription ItemPurged{ itemPurged(subscriptionId:$id){ id } }" );
		Query( Ƒ("mutation addItemLabel( id:{}, labelId:{} )", item, label) );
		try{
			Query( Ƒ("mutation purgeItem( id:{} )", item) );
			ADD_FAILURE() << "the parent was purged from under its children";
		}
		catch( const DB::DBException& e ){
			EXPECT_EQ( e.Error, DB::EDbError::ForeignKey ) << e.what();
		}
		EXPECT_EQ( Labels(item), (vector<string>{"map-purge-label"}) );
		EXPECT_TRUE( purged->Changes.empty() );

		Query( Ƒ("mutation removeItemLabel( id:{}, labelId:{} )", item, label) );
		Query( Ƒ("mutation purgeItem( id:{} )", item) );
		EXPECT_EQ( Count("items", "where item_id=?", {DB::Value{item}}), 0u );
		ASSERT_EQ( purged->Changes.size(), 1u );
		EXPECT_EQ( AsNumber<uint>(purged->Body(0), "id"), item );
	}
}
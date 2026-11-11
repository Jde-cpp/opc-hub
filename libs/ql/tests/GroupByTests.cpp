//Views 6a (#594):  `groupBy`, a count per value of one column - `items( groupBy:"itemType" ){ itemType count }` - through LocalQL
//against the qlTests schema on sqlite.  Each test filters to its own rows (`grp-…` slugs), which the fixture purges.  sqlite
//takes a select-list column that is not grouped and SQL Server does not, so the enum case reads the statement as well.
#include "globals.h"

#define let const auto

namespace Jde::QL::Tests{
	using namespace Json;

	struct GroupByTests : OpsFixture{
		α SetUp()->void override{
			Item( "grp-a", Gadget, ", attributes:1" );
			Item( "grp-b", Gadget, ", attributes:1" );
			Item( "grp-c", Gizmo );
			Item( "grp-d", Gadget, ", attributes:2" );
		}
		Ω Groups( sv column, sv extra={} )ε->jarray{
			return QueryArray( Ƒ(R"(items( groupBy:"{0}", slug:{{glob:"grp-*"}}{1} ){{ {0} count }})", column, extra) );
		}
		Ω What( sv args, sv columns )ι->string{ return WhatOf( Ƒ("items( {} ){{ {} }}", args, columns) ); }
		Ω Status( std::invocable auto&& f )ι->EHttpStatus{
			try{ f(); }
			catch( const Exception& e ){ return e.HttpStatus(); }
			return EHttpStatus::None;
		}
	};

	TEST_F( GroupByTests, ACountPerValueWithTheNullGroupFirst ){
		let groups = Groups( "attributes" );
		ASSERT_EQ( groups.size(), 3u ) << serialize( groups );
		EXPECT_TRUE( groups[0].at("attributes").is_null() ) << serialize( groups );
		EXPECT_EQ( AsNumber<uint>(groups[0].get_object(), "count"), 1u );
		EXPECT_EQ( AsNumber<uint>(groups[1].get_object(), "attributes"), 1u );
		EXPECT_EQ( AsNumber<uint>(groups[1].get_object(), "count"), 2u );
		EXPECT_EQ( AsNumber<uint>(groups[2].get_object(), "attributes"), 2u );
		EXPECT_EQ( AsNumber<uint>(groups[2].get_object(), "count"), 1u );
	}
	TEST_F( GroupByTests, AFilterNarrowsTheCounts ){
		let groups = Groups( "attributes", ", attributes:{gt:0}" );
		ASSERT_EQ( groups.size(), 2u ) << serialize( groups );
		EXPECT_EQ( AsNumber<uint>(groups[0].get_object(), "count"), 2u );
		let one = Groups( "itemType", R"(, itemType:"Gizmo")" );
		ASSERT_EQ( one.size(), 1u ) << serialize( one );
		EXPECT_EQ( AsNumber<uint>(one[0].get_object(), "count"), 1u );
	}
	//the grouped column is the one selected, the enum table's name, not items.item_type_id alone, which the filter and the sort
	//resolve to.  The id is grouped after it, since a foreign key's target name need not be unique.
	TEST_F( GroupByTests, AnEnumGroupsByItsDisplayName ){
		let groups = Groups( "itemType" );
		ASSERT_EQ( groups.size(), 2u ) << serialize( groups );
		EXPECT_EQ( AsSV(groups[0].get_object(), "itemType"), Gadget );
		EXPECT_EQ( AsNumber<uint>(groups[0].get_object(), "count"), 3u );
		EXPECT_EQ( AsSV(groups[1].get_object(), "itemType"), Gizmo );
		EXPECT_EQ( AsNumber<uint>(groups[1].get_object(), "count"), 1u );

		auto statement = SelectStatement( ParseQuery(R"(items( groupBy:"itemType" ){ count itemType })", {}, Schemas()) );
		EXPECT_EQ( statement.GroupBy, "qlt_item_types.name, qlt_items.item_type_id" );
		let sql = statement.Move().Text;
		EXPECT_NE( sql.find("\ngroup by qlt_item_types.name, qlt_items.item_type_id\norder by qlt_item_types.name, qlt_items.item_type_id"), string::npos ) << sql;
	}
	TEST_F( GroupByTests, AColumnOfItsOwnGroupsAlone ){
		auto statement = SelectStatement( ParseQuery(R"(items( groupBy:"attributes" ){ attributes count })", {}, Schemas()) );
		EXPECT_EQ( statement.GroupBy, "qlt_items.attributes" );
	}
	TEST_F( GroupByTests, AnAliasedPairAnswersBoth ){
		let y = QueryObject( R"({ types: items( groupBy:"itemType", slug:{glob:"grp-*"} ){ itemType count } attrs: items( groupBy:"attributes", slug:{glob:"grp-*"} ){ attributes count } })" );
		EXPECT_EQ( AsArray(y, "types").size(), 2u ) << serialize( y );
		EXPECT_EQ( AsArray(y, "attrs").size(), 3u ) << serialize( y );
	}
	TEST_F( GroupByTests, Refusals ){
		EXPECT_NE( What(R"(groupBy:"nope")", "itemType count").find("not 'itemType'"), string::npos );
		EXPECT_NE( What(R"(groupBy:"nope")", "count").find("needs 'nope'"), string::npos );
		for( sv paging : {R"(orderBy:"name")", "limit:2", "skip:1", "offset:1"} )
			EXPECT_NE( What(Ƒ(R"(groupBy:"itemType", {})", paging), "itemType count").find("no orderBy, limit or skip"), string::npos ) << paging;
		EXPECT_NE( What(R"(groupBy:"itemType")", "itemType name count").find("not 'name'"), string::npos );
		EXPECT_NE( What(R"(groupBy:"itemType")", "count").find("needs 'itemType'"), string::npos );
		EXPECT_NE( What(R"(groupBy:["itemType","attributes"])", "itemType count").find("one column's name"), string::npos );
		EXPECT_NE( WhatOf(R"(item( groupBy:"itemType" ){ itemType count })").find("collection"), string::npos );
	}
	//refused before any handler, as a custom query builds its children itself, and at any depth.
	TEST_F( GroupByTests, ASubTableIsRefused ){
		EXPECT_NE( WhatOf(R"(items( slug:{glob:"grp-*"} ){ id labels( groupBy:"name" ){ name count } })").find("sub-table 'labels' takes no groupBy"), string::npos );
		EXPECT_NE( WhatOf(R"(itemTypes{ id items{ id labels( groupBy:"name" ){ name count } } })").find("sub-table 'labels' takes no groupBy"), string::npos );
	}
	TEST_F( GroupByTests, ASubscriptionIsRefused ){
		EXPECT_NE( WhatOf([&]{ Listen(R"(subscription ItemCreated{ itemCreated( subscriptionId:$id, groupBy:"itemType" ){ itemType count } })"); }).find("takes no groupBy"), string::npos );
		EXPECT_NE( WhatOf([&]{ Listen(R"(subscription ItemCreated{ itemCreated( subscriptionId:$id ){ id labels( groupBy:"name" ){ name count } } })"); }).find("takes no groupBy"), string::npos );
	}
	TEST_F( GroupByTests, RefusalsAreBadRequests ){
		for( sv ql : {R"(items( groupBy:"itemType", limit:2 ){ itemType count })", R"(items( groupBy:"" ){ count })", R"(items{ id labels( groupBy:"name" ){ name count } })"} )
			EXPECT_EQ( Status([&]{ Query(string{ql}); }), EHttpStatus::BadRequest ) << ql;
		EXPECT_EQ( Status([&]{ Listen(R"(subscription ItemCreated{ itemCreated( subscriptionId:$id, groupBy:"itemType" ){ itemType count } })"); }), EHttpStatus::BadRequest );
	}
}
//#238:  ToWhereClause, the SQL half of QL::Filter - FilterTests covers the in-memory half and says in its own header that this
//one needs a DB::Table behind a syntax.  The qlTests schema's items table on sqlite is both, so every filter shape the parser
//produces (Input::Filter) is rendered here and read back as the clause and its bound params - the dialect's spellings
//(SqliteSyntax), which are also what CrudOpsTests' selects run.  The clause is built from the parse alone, except an enum name's:
//that resolves to its id through item_types, so AnEnumFiltersByDisplayName needs the data source and EnsureItemTypes' rows.
#include <gtest/gtest.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/FilterQL.h>
#include "globals.h"

#define let const auto

namespace Jde::QL::Tests{
	Ω where( sv query, jobject vars={} )ε->DB::WhereClause{
		let table = ParseQuery( string{query}, move(vars), Schemas() );
		return ToWhereClause( table, *table.DBTable() );
	}
	Ω whatOf( sv query )ι->string{ return WhatOf( [=]{ where(query); } ); }//the clause's refusal.

	TEST( WhereClauseTests, NoFilterIsNoClause ){
		let w = where( "items{ id name }" );
		EXPECT_EQ( w.ToString(), "" );
		EXPECT_TRUE( w.Empty() );
		EXPECT_TRUE( w.Params().empty() );
	}
	//a scalar is equality, and `id` is the primary key whatever the column is called.
	TEST( WhereClauseTests, ScalarIsEqualityAndIdIsThePrimaryKey ){
		let byName = where( R"(items( name:"a" ){ id })" );
		EXPECT_EQ( byName.ToString(), "where qlt_items.name=?" );
		ASSERT_EQ( byName.Params().size(), 1u );
		EXPECT_EQ( byName.Params()[0].get_string(), "a" );

		let byId = where( "items( id:42 ){ id }" );
		EXPECT_EQ( byId.ToString(), "where qlt_items.item_id=?" );
		ASSERT_EQ( byId.Params().size(), 1u );
		EXPECT_EQ( byId.Params()[0].ToUInt(), 42u );
		EXPECT_EQ( where("itemTypes( id:1 ){ id }").ToString(), "where qlt_item_types.item_type_id=?" );//a pk that is no sequence.
	}
	//`deleted:null` is how a caller asks for the live rows - there is no implicit `deleted is null` (SelectStatementTests).
	TEST( WhereClauseTests, NullIsIsNull ){
		let w = where( "items( deleted:null ){ id }" );
		EXPECT_EQ( w.ToString(), "where qlt_items.deleted is null" );
		EXPECT_TRUE( w.Params().empty() );
		EXPECT_EQ( where("items( deleted:{ne:null} ){ id }").ToString(), "where qlt_items.deleted is not null" );
		EXPECT_NE( whatOf("items( deleted:{gt:null} ){ id }").find("Null value not allowed"), string::npos );
	}
	//several columns and together, in column-name order - ColumnFilters is a flat_map, so not the order they were written in.
	TEST( WhereClauseTests, ColumnsAndTogetherByName ){
		let w = where( R"(items( name:"a", deleted:null, id:1 ){ id })" );
		EXPECT_EQ( w.ToString(), "where qlt_items.deleted is null and qlt_items.item_id=? and qlt_items.name=?" );
		ASSERT_EQ( w.Params().size(), 2u );
		EXPECT_EQ( w.Params()[0].ToUInt(), 1u );
		EXPECT_EQ( w.Params()[1].get_string(), "a" );
	}
	TEST( WhereClauseTests, ArrayIsIn ){
		let w = where( "items( id:[1,2,3] ){ id }" );
		EXPECT_EQ( w.ToString(), "where qlt_items.item_id in(?,?,?)" );
		ASSERT_EQ( w.Params().size(), 3u );
		EXPECT_EQ( w.Params()[2].ToUInt(), 3u );
	}
	//a null among the members is `or is null`, alone it is `is null`, and under nin each is its complement.
	TEST( WhereClauseTests, NullInAnArray ){
		let some = where( R"(items( name:["a",null] ){ id })" );
		EXPECT_EQ( some.ToString(), "where (qlt_items.name in(?) or qlt_items.name is null)" );
		ASSERT_EQ( some.Params().size(), 1u );
		EXPECT_EQ( where("items( name:[null] ){ id }").ToString(), "where qlt_items.name is null" );
		EXPECT_EQ( where(R"(items( name:{nin:["a",null]} ){ id })").ToString(), "where (qlt_items.name not in(?) and qlt_items.name is not null)" );
		EXPECT_EQ( where("items( name:{nin:[null]} ){ id }").ToString(), "where qlt_items.name is not null" );
	}
	//the operator object:  one clause per member, in the order written, each with the dialect's spelling.
	TEST( WhereClauseTests, OperatorsAreOneClauseEach ){
		let w = where( "items( id:{gt:1, lte:5} ){ id }" );
		EXPECT_EQ( w.ToString(), "where qlt_items.item_id>? and qlt_items.item_id<=?" );
		ASSERT_EQ( w.Params().size(), 2u );
		EXPECT_EQ( w.Params()[0].ToUInt(), 1u );
		EXPECT_EQ( w.Params()[1].ToUInt(), 5u );
		EXPECT_EQ( where(R"(items( name:{eq:"a"} ){ id })").ToString(), "where qlt_items.name=?" );
		EXPECT_EQ( where(R"(items( name:{ne:"a"} ){ id })").ToString(), "where qlt_items.name!=?" );
		EXPECT_EQ( where("items( id:{gte:1} ){ id }").ToString(), "where qlt_items.item_id>=?" );
		EXPECT_EQ( where("items( id:{lt:1} ){ id }").ToString(), "where qlt_items.item_id<?" );
		EXPECT_EQ( where("items( id:{in:[1,2]} ){ id }").ToString(), "where qlt_items.item_id in(?,?)" );
		EXPECT_EQ( where("items( id:{nin:[1,2]} ){ id }").ToString(), "where qlt_items.item_id not in(?,?)" );
	}
	//#590: `not in` is unknown for a null, so on a nullable column a nin without a null keeps the null rows;  one with a null,
	//and any on a column that has none, are as they were.  A lone value is a list of one.
	TEST( WhereClauseTests, NinKeepsANullableColumnsNulls ){
		let w = where( R"(labels( color:{nin:["red","blue"]} ){ id })" );
		EXPECT_EQ( w.ToString(), "where (qlt_labels.color not in(?,?) or qlt_labels.color is null)" );
		ASSERT_EQ( w.Params().size(), 2u );
		EXPECT_EQ( w.Params()[1].get_string(), "blue" );
		EXPECT_EQ( where(R"(labels( color:{nin:"red"} ){ id })").ToString(), "where (qlt_labels.color not in(?) or qlt_labels.color is null)" );
		EXPECT_EQ( where(R"(labels( color:{nin:["red",null]} ){ id })").ToString(), "where (qlt_labels.color not in(?) and qlt_labels.color is not null)" );
		EXPECT_EQ( where("labels( color:{nin:[]} ){ id }").ToString(), "where 1=1" );
		EXPECT_EQ( where(R"(labels( name:{nin:["a"]} ){ id })").ToString(), "where qlt_labels.name not in(?)" );//not nullable.
		EXPECT_EQ( where(R"(labels( color:{in:["red"]} ){ id })").ToString(), "where qlt_labels.color in(?)" );//`in` never matched a null.
	}
	//#590 review #6: `ne` is a `nin` of one, so on a nullable column it keeps the null rows too, as the in-memory NotEqual does.
	TEST( WhereClauseTests, NeKeepsANullableColumnsNulls ){
		let w = where( R"(labels( color:{ne:"red"} ){ id })" );
		EXPECT_EQ( w.ToString(), "where (qlt_labels.color!=? or qlt_labels.color is null)" );
		ASSERT_EQ( w.Params().size(), 1u );
		EXPECT_EQ( w.Params()[0].get_string(), "red" );
		EXPECT_EQ( where(R"(labels( color:{ne:["red"]} ){ id })").ToString(), "where (qlt_labels.color!=? or qlt_labels.color is null)" );
		EXPECT_EQ( where(R"(labels( color:{ne:null} ){ id })").ToString(), "where qlt_labels.color is not null" );
		EXPECT_EQ( where(R"(labels( color:{ne:[null]} ){ id })").ToString(), "where qlt_labels.color is not null" );
		EXPECT_EQ( where(R"(labels( name:{ne:"a"} ){ id })").ToString(), "where qlt_labels.name!=?" );//not nullable.
	}
	//the filter box merges a column's terms into one operator object:  `in` beside `nin`, and a variable under each operator.
	TEST( WhereClauseTests, InAndNinShareAnOperatorObject ){
		let w = where( R"(labels( color:{in:["red","blue"], nin:["blue"]} ){ id })" );
		EXPECT_EQ( w.ToString(), "where qlt_labels.color in(?,?) and (qlt_labels.color not in(?) or qlt_labels.color is null)" );
		ASSERT_EQ( w.Params().size(), 3u );
		EXPECT_EQ( w.Params()[2].get_string(), "blue" );
	}
	TEST( WhereClauseTests, TwoVariablesInOneOperatorObject ){
		let w = where( "items( id:{gte:$id_gte, lt:$id_lt}, name:{in:$name_in, nin:$name_nin} ){ id }", jobject{{"id_gte",3}, {"id_lt",9}, {"name_in",jarray{"a","b"}}, {"name_nin",jarray{"b"}}} );
		EXPECT_EQ( w.ToString(), "where qlt_items.item_id>=? and qlt_items.item_id<? and qlt_items.name in(?,?) and qlt_items.name not in(?)" );
		ASSERT_EQ( w.Params().size(), 5u );
		EXPECT_EQ( w.Params()[0].ToUInt(), 3u );
		EXPECT_EQ( w.Params()[1].ToUInt(), 9u );
		EXPECT_EQ( w.Params()[4].get_string(), "b" );
		EXPECT_THROW( where("items( id:{gte:$id_gte, lt:$id_lt} ){ id }", jobject{{"id_gte",3}}), Exception );//each is bound, or the query is refused.
	}
	//an array under a scalar operator is refused at build - one value, one placeholder - rather than bound askew.
	TEST( WhereClauseTests, AnArrayUnderAScalarOperatorIsRefused ){
		EXPECT_NE( whatOf("items( id:{eq:[1,2]} ){ id }").find("single value"), string::npos );
		EXPECT_NE( whatOf("items( id:{gt:[1,2]} ){ id }").find("single value"), string::npos );
	}
	//`glob:` is the one pattern language the filters speak, and sqlite takes it verbatim - the clause and QL::globMatch agree.
	TEST( WhereClauseTests, GlobBindsThePatternVerbatim ){
		let w = where( R"(items( name:{glob:"a*[bc]?"} ){ id })" );
		EXPECT_EQ( w.ToString(), "where qlt_items.name glob ?" );
		ASSERT_EQ( w.Params().size(), 1u );
		EXPECT_EQ( w.Params()[0].get_string(), "a*[bc]?" );
		EXPECT_THROW( where("items( name:{glob:42} ){ id }"), Exception );//not a string.
	}
	//#590: a list under `glob` is one clause a pattern, all of which must match - the filter box's bare words.
	TEST( WhereClauseTests, AGlobListIsEveryPattern ){
		let w = where( R"(items( name:{glob:["*pump*","*station*"]} ){ id })" );
		EXPECT_EQ( w.ToString(), "where (qlt_items.name glob ? and qlt_items.name glob ?)" );
		ASSERT_EQ( w.Params().size(), 2u );
		EXPECT_EQ( w.Params()[0].get_string(), "*pump*" );
		EXPECT_EQ( w.Params()[1].get_string(), "*station*" );
		EXPECT_EQ( where(R"(items( name:{glob:["*pump*"]} ){ id })").ToString(), "where qlt_items.name glob ?" );
		let listed = where( "items( name:{glob:$words}, id:{gt:1} ){ id }", jobject{{"words",jarray{"*a*","*b*","*c*"}}} );//a variable's list, and the next column's param after it.
		EXPECT_EQ( listed.ToString(), "where qlt_items.item_id>? and (qlt_items.name glob ? and qlt_items.name glob ? and qlt_items.name glob ?)" );
		ASSERT_EQ( listed.Params().size(), 4u );
		EXPECT_EQ( listed.Params()[3].get_string(), "*c*" );
		EXPECT_NE( whatOf("items( name:{glob:[]} ){ id }").find("at least one pattern"), string::npos );
		EXPECT_THROW( where(R"(items( name:{glob:["a*",42]} ){ id })"), Exception );//each a string.
	}
	//sqlite parses REGEXP but ships no implementation - refused at build, naming the operator that does work.
	TEST( WhereClauseTests, RegexIsRefusedBySqlite ){
		let what = whatOf( R"(items( name:{regex:"^a"} ){ id })" );
		EXPECT_NE( what.find("regex"), string::npos ) << what;
		EXPECT_NE( what.find("glob"), string::npos ) << what;
	}
	//#20: an enum column filtered by its display name - the only spelling a client has - resolves to the id through the enum
	//table, and the clause is on the id column;  the id spelling still works, and a name the enum lacks is refused by name.
	TEST( WhereClauseTests, AnEnumFiltersByDisplayName ){
		let byName = where( Ƒ(R"(items( itemType:"{}" ){{ id }})", Gizmo) );
		EXPECT_EQ( byName.ToString(), "where qlt_items.item_type_id=?" );
		ASSERT_EQ( byName.Params().size(), 1u );
		EXPECT_EQ( byName.Params()[0].ToUInt(), 2u );
		EXPECT_EQ( where("items( itemTypeId:2 ){ id }").ToString(), "where qlt_items.item_type_id=?" );
		let what = whatOf( R"(items( itemType:"Doohickey" ){ id })" );
		EXPECT_NE( what.find("Doohickey"), string::npos ) << what;
		EXPECT_NE( what.find("item_types"), string::npos ) << what;
	}
	//a time literal binds as a time, so the comparison is chronological - the in-memory half's TestTime, on the SQL side.
	TEST( WhereClauseTests, ATimeLiteralBindsAsATime ){
		let w = where( R"(items( created:{gte:"2026-10-13T00:00:00Z"} ){ id })" );
		EXPECT_EQ( w.ToString(), "where qlt_items.created>=?" );
		ASSERT_EQ( w.Params().size(), 1u );
		EXPECT_EQ( w.Params()[0].Type(), DB::EValue::Time );
	}
	//limit/offset/skip/orderBy are the statement's, not the where clause's - SelectStatement reads them from the args itself.
	TEST( WhereClauseTests, PagingKeysAreNotPredicates ){
		EXPECT_EQ( where(R"(items( limit:10, offset:5, skip:5, orderBy:"name" ){ id })").ToString(), "" );
		EXPECT_EQ( where(R"(items( limit:10, name:"a" ){ id })").ToString(), "where qlt_items.name=?" );
	}
	TEST( WhereClauseTests, AVariableIsItsValue ){
		let w = where( "items( name:$name ){ id }", jobject{{"name","bound"}} );
		EXPECT_EQ( w.ToString(), "where qlt_items.name=?" );
		ASSERT_EQ( w.Params().size(), 1u );
		EXPECT_EQ( w.Params()[0].get_string(), "bound" );
		EXPECT_THROW( where("items( name:$name ){ id }"), Exception );//#36: unbound is an error, not a null.
	}
	//an unknown column is refused naming the table and the column.
	TEST( WhereClauseTests, AnUnknownColumnIsNamed ){
		let what = whatOf( "items( bogus:1 ){ id }" );
		EXPECT_NE( what.find("bogus"), string::npos ) << what;
		EXPECT_NE( what.find("items"), string::npos ) << what;
	}
}
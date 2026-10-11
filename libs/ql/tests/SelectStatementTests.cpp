//review3 #3:  the three ways a nested table reached a null pointer in columnSql/SelectSubTables -
//  * a system-named child of a real table ("providers{ id status{ x } }") got FindView (null) instead of GetViewPtr (throw),
//    because Parser::LoadTable re-derived system-ness per child instead of inheriting it;
//  * findFK's `ToSingular(name)+"_id"` guess spells a table's own pk when it is nested under itself, and that column has no
//    PKTable, so `pFK->PKTable->GetPK()` dereferenced null;
//  * a name the guess can not spell handed a null `Join.To` to FromClause::TryAdd.
//Schema-only:  a '_'-prefixed DBSchema is QL-only, so no data source, syntax or catalog is involved - QL::SelectStatement is
//columnSql's caller and the crash site (SelectAwait.cpp SelectStatement -> columnSql), so the statement is built, not run.
#include <gtest/gtest.h>
#include <jde/ql/ql.h>
#include "UnitSchema.h"

#define let const auto

namespace Jde::QL::Tests{
	Ω statement( string text )ε->DB::Statement{
		let schema = schemas();
		return QL::SelectStatement( QL::ParseQuery(move(text), {}, schema) );
	}

	//The where clause is exactly the query's filters:  no implicit `deleted is null`, so a caller that wants the live rows says
	//deleted:null (Access' delete/restore tests cover that half - a filter needs a syntax, which this schema has none of).
	TEST( SelectStatementTests, DeletedRowsAreNotFilteredUnlessAsked ){
		EXPECT_EQ( statement("widgets{ id name }").Where.ToString(), "" );
		EXPECT_EQ( statement("widgets{ id name deleted }").Where.ToString(), "" );
	}

	//the control:  a real fk still joins, so none of the guards below cost a working query.
	TEST( SelectStatementTests, NestedForeignKeyStillJoins ){
		auto sql = statement( "providers{ id providerTypes{ id name } }" ); //not const: FromClause::Contains is non-const.
		EXPECT_TRUE( sql.From.Contains("provider_types") );
		EXPECT_TRUE( sql.From.HasJoin() );
	}

	//"status" is system-shaped, so the child used to resolve through FindView and carry a null table into columnSql.
	//Inheriting the parent's system flag sends it to GetViewPtr instead, which names what it could not find.
	TEST( SelectStatementTests, SystemNamedChildOfARealTableThrows ){
		for( let text : {"providers{ id status{ x } }", "providers{ id __x{ id } }"} ){
			try{
				statement( text );
				ADD_FAILURE() << text;
			}
			catch( const Exception& e ){
				EXPECT_NE( string{e.what()}.find("Could not find view"), string::npos ) << e.what(); //refused at parse, by the layer that knows the name.
			}
		}
	}

	//__type's children legitimately resolve to no view; that inheritance is what the fix keeps.
	TEST( SelectStatementTests, SystemParentStillCarriesItsChildren ){
		let schema = schemas();
		let ql = QL::ParseQuery( R"(__type(name:"logTags"){ fields{ name } })", {}, schema );
		ASSERT_EQ( ql.Tables.size(), 1u );
		EXPECT_EQ( ql.Tables[0].JsonName, "fields" );
		EXPECT_FALSE( ql.Tables[0].DBTable() ); //no view, and none wanted.
	}

	//the second layer:  a null child table built by hand (hooks and LocalQL construct TableQL directly) is an error, not a deref.
	TEST( SelectStatementTests, NullChildTableThrowsInsteadOfDereferencing ){
		let schema = schemas();
		auto ql = QL::ParseQuery( "providers{ id }", {}, schema );
		ql.Tables.emplace_back( "status", jobject{}, ms<jobject>(), schema, true ); //system -> FindView -> null.
		try{
			QL::SelectStatement( ql );
			FAIL() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("Unknown sub-table"), string::npos ) << e.what(); //the columnSql layer, not the parser's.
		}
	}

	//#7: asking for a column by its enum stem walks <name>_id's pk table for the display name.  provider_types has no
	//provider_type column and its own pk *is* provider_type_id, which has no pk table - the same shape as users_ql.identity_id
	//and providers.provider_id.  It is a column that does not exist, and now says so instead of dereferencing null.
	TEST( SelectStatementTests, EnumStemWithNoPkTableIsAnUnknownColumn ){
		try{
			statement( "providerTypes{ id providerType }" );
			FAIL() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("provider_type"), string::npos ) << e.what(); //named, not a segfault.
		}
	}
	//the control the finding names:  a stem whose <name>_id *does* carry a pkTable still resolves to that table's name column.
	TEST( SelectStatementTests, EnumStemWithAPkTableStillResolves ){
		auto sql = statement( "providers{ id providerType }" );
		EXPECT_TRUE( sql.From.Contains("provider_types") ); //joined for the display name.
	}

	//provider_types nested under itself:  the guess spells provider_type_id, its own pk, whose PKTable is null.
	TEST( SelectStatementTests, SelfNestedTableDoesNotMatchItsOwnPk ){
		auto sql = statement( "providerTypes{ id providerTypes{ id } }" );
		EXPECT_FALSE( sql.From.HasJoin() ); //no relation found, so the child is skipped - the pre-existing behaviour for an unrelated child.
	}
}

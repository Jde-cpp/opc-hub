#include <gtest/gtest.h>
#include <jde/db/generators/FromClause.h>
#include <jde/db/generators/InsertClause.h>
#include <jde/db/generators/Object.h>

namespace Jde::DB::Tests{
	//db-review2 #26: InsertClause::Proc placeholder count must match the params (was a leading "(?" -> zero Values gave `name(?)` with 0 params), and SequenceColumn must not deref a null column.
	TEST( InsertClauseTests, ProcParamCountMatches ){
		EXPECT_EQ( InsertClause( "p", vector<Value>{} ).Move().Text, "p()" );          //0 values -> no placeholder (was "p(?)").
		EXPECT_TRUE( InsertClause( "p", vector<Value>{} ).Move().Params.empty() );
		auto sql = InsertClause( "p", vector<Value>{ Value{1}, Value{2} } ).Move();
		EXPECT_EQ( sql.Text, "p(?,?)" );
		EXPECT_EQ( sql.Params.size(), 2u );
	}
	TEST( InsertClauseTests, SequenceColumnIsNullForProcParams ){
		InsertClause ins{ "p", vector<Value>{ Value{1} } }; //proc ctor -> Values have null columns.
		EXPECT_EQ( ins.SequenceColumn(), nullptr );          //was a null-column ->Table deref (segfault).
	}

	//ql-review3 #3: TryAdd read join.To->Table->Name unconditionally, but a null To is a legal Join - it is the single-table
	//form operator+= merges into, and Contains/GetColumnPtr both allow for it.  A sub-table whose fk could not be resolved
	//passed one in and segfaulted here rather than at the caller.
	TEST( FromClauseTests, TryAddNullToDoesNotDereference ){
		FromClause from;
		from.TryAdd( Join{ms<Column>("a"), nullptr, false} );
		ASSERT_EQ( from.Joins.size(), 1u );
		EXPECT_FALSE( from.HasJoin() ); //a single-table from clause, not a join.
	}

	//db-review2 #2: Value had no operator==, so Object's Value/Values compare converted back to Object and recursed - a
	//stack overflow at the first compared one.  ValueTests.Equality pins the operator; this pins the site.
	TEST( ObjectTests, ValueEquality ){
		const DB::Object a = DB::Value{5}, b = DB::Value{5}, c = DB::Value{6};
		EXPECT_TRUE( a==b );
		EXPECT_FALSE( a==c );

		const DB::Object v1 = vector<DB::Value>{ DB::Value{1}, DB::Value{2} };
		const DB::Object v2 = vector<DB::Value>{ DB::Value{1}, DB::Value{2} };
		const DB::Object v3 = vector<DB::Value>{ DB::Value{1} };
		EXPECT_TRUE( v1==v2 );
		EXPECT_FALSE( v1==v3 );
		EXPECT_FALSE( a==v1 ); //different alternatives.
	}

	//db-review2 #25: an Object holding Values must render placeholders (?,?), not the literal `[ 1, 2]` - GetParams appends the values, so the counts must match.
	TEST( ObjectTests, ValuesRendersPlaceholders ){
		DB::Object o = vector<DB::Value>{ DB::Value{1}, DB::Value{2}, DB::Value{3} };
		EXPECT_EQ( DB::ToString(o), "(?,?,?)" );
		EXPECT_EQ( DB::GetParams(o).size(), 3u ); //placeholder count now matches param count.
	}
}
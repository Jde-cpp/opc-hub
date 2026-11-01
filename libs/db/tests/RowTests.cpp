#include <gtest/gtest.h>
#include <jde/db/Row.h>

namespace Jde::DB::Tests{
	//db-refactor B6: GetString is const-only now - the old mutable overload turned a NULL cell into an empty *string* as a side effect
	//of reading it, so IsNull flipped under a caller that read first and tested after.  TakeString is the consuming read,
	//named as one: it moves the cell out, and a NULL answers empty and stays NULL.
	TEST( RowTests, TakeStringConsumesAndNullStaysNull ){
		Row row{ vector<Value>{ Value{string{"abc"}}, Value{} } };
		EXPECT_EQ( row.GetString(0), "abc" ); EXPECT_EQ( row.GetString(0), "abc" ); //a read is a read.
		EXPECT_EQ( row.TakeString(0), "abc" );
		EXPECT_TRUE( row.GetString(0).empty() );  //taken.
		EXPECT_FALSE( row.IsNull(0) );            //an empty string, not a NULL - the cell was a string and still is.
		EXPECT_TRUE( row.GetString(1).empty() && row.IsNull(1) );
		EXPECT_TRUE( row.TakeString(1).empty() && row.IsNull(1) ); //NULL stays NULL through the consuming read too.
	}

	//db-review3 #54: the old GetUIntOpt read with get_uint(), which demands the *exact* uint alternative - so on a cell holding int64 it
	//ASSERTed (log-only in every build) and handed back an engaged optional{0}, where GetUInt converted.  It matters because
	//sqlite delivers every integer as int64 (SqliteRow.cpp's sqlite3_int64) and MySQL's signed INT columns are int64 kind
	//too.  db-refactor A1 deleted the sixteen typed twins; Get<T>/GetOpt<T> are the one path, so this now pins that the
	//two templates convert across alternatives the same way.
	//db-review3 #58: the invariant worth pinning is the pairing - for a non-null cell GetOpt<T> must answer exactly what Get<T>
	//answers, for every T a caller reaches for, and for a null cell nullopt.  GetBit/GetBitOpt and GetString are the
	//accessors that survived db-refactor A1 because they do something Get<T> does not (bool-or-uint8; null-to-empty), so they are
	//pinned alongside.
	TEST( RowTests, EveryOptAccessorMatchesItsTwin ){
		const auto now = DBTimePoint{ std::chrono::floor<std::chrono::seconds>(DBClock::now()) };
		const Row row{ vector<Value>{ Value{(_int)42}, Value{(uint32_t)7}, Value{true}, Value{2.5}, Value{string{"s"}}, Value{}, Value{now}, Value{(uint)9} } };
		constexpr uint nul = 5;

		EXPECT_EQ( row.GetOpt<_int>(0).value_or(0), row.Get<_int>(0) );
		EXPECT_EQ( row.GetOpt<int32_t>(0).value_or(0), row.Get<int32_t>(0) );
		EXPECT_EQ( row.GetOpt<uint>(0).value_or(0), row.Get<uint>(0) );      //#54's case: an int64 cell read as uint.
		EXPECT_EQ( row.GetOpt<uint>(7).value_or(0), row.Get<uint>(7) );      //and the exact alternative.
		EXPECT_EQ( row.GetOpt<uint32_t>(1).value_or(0), row.Get<uint32_t>(1) );
		EXPECT_EQ( row.GetOpt<uint8>(1).value_or(0), row.Get<uint8>(1) );
		EXPECT_EQ( row.GetOpt<uint16_t>(1).value_or(0), row.Get<uint16_t>(1) );
		EXPECT_EQ( row.GetBitOpt(2).value_or(false), row.GetBit(2) );
		EXPECT_EQ( row.GetOpt<double>(3).value_or(0), row.Get<double>(3) );
		EXPECT_EQ( row.GetOpt<string>(4).value_or(""), row.GetString(4) );
		EXPECT_EQ( row.GetOpt<DBTimePoint>(6).value_or(DBTimePoint{}), row.Get<DBTimePoint>(6) ); //db-refactor A1: Value::Get grew a DBTimePoint branch for the old GetTimePoint callers.
		EXPECT_EQ( row.Get<DBTimePoint>(6), now );
		EXPECT_EQ( row.Get<uint>(0), 42u );
		EXPECT_EQ( row.Get<uint>(7), 9u );

		//the null cell: every Opt form answers nullopt, none of them guesses.
		EXPECT_TRUE( row.IsNull(nul) );
		EXPECT_FALSE( row.GetOpt<_int>(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<int32_t>(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<uint>(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<uint32_t>(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<uint8>(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<uint16_t>(nul).has_value() );
		EXPECT_FALSE( row.GetBitOpt(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<double>(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<DBTimePoint>(nul).has_value() );
		EXPECT_FALSE( row.GetOpt<string>(nul).has_value() );
		EXPECT_EQ( row.Size(), 8u );
	}
}
#include <gtest/gtest.h>
#include <jde/db/Value.h>
#include <jde/fwk/chrono.h> //Chrono::ToTimePoint, for the DateTime expectation.

#define let const auto

namespace Jde::DB::Tests{
	//db-review2 #22: Value::operator=(uint) must return Value& (was `auto` -> deduced Value, i.e. a by-value copy).
	static_assert( std::is_same_v<decltype(std::declval<Value&>() = uint{5}), Value&>, "operator=(uint) must return Value&, not a copy." );

	//db-review3 #34: every `type:` string common-meta.libsonnet exports has to resolve - an unmapped one silently becomes
	//EType::None, and ColumnDdl then emits a column with no type at all ("x  not null"): a syntax error on MySQL, a
	//typeless BLOB-affinity column on sqlite.  int16/smallFloat/blob were the three that did not.
	//db-review3 #58: Value(EType, const jvalue&) is the path every QL insert/update/filter takes, and had no coverage at all - which
	//is how db-review3 #3 (DateTime arriving as a string) went unnoticed.  One case per branch of fromJson, keyed on the EValue the
	//alternative ends up as, since that is what the drivers bind on.
	TEST( ValueTests, FromJsonEveryEType ){
		using enum EType;
		let alt = []( EType type, const jvalue& j ){ return Value{type, j, SRCE_CUR}.Type(); };

		for( let& type : {VarChar, VarWChar, NText, Text, Uri} )
			EXPECT_EQ( alt(type, jvalue{"x"}), EValue::String ) << (uint)type;
		EXPECT_EQ( alt(Bit, jvalue{true}), EValue::Bool );
		EXPECT_EQ( alt(Int8, jvalue{-3}), EValue::Int8 );
		for( let& type : {Int16, Int} )
			EXPECT_EQ( alt(type, jvalue{-5}), EValue::Int32 ) << (uint)type;
		EXPECT_EQ( alt(Long, jvalue{-6}), EValue::Int64 );
		//The variant has no uint8/uint16 member.  uint8 is unsigned char, which promotes to `int` on every platform.
		//UInt16/UInt bind as exact-width uint32_t ([`Value.cpp:30`]): their fast typedefs are 64-bit on glibc, so binding
		//those would pick the uint64 alternative on Linux but uint32 on Windows - value-preserving either way, but
		//platform-dependent, and the alternative is what every driver binds on.  Asserted as the fixed width rather than
		//derived from sizeof, so that a return to the fast typedefs fails here instead of silently changing what Linux binds.
		EXPECT_EQ( alt(UInt8, jvalue{7}), EValue::Int32 );
		EXPECT_EQ( alt(UInt16, jvalue{8}), EValue::UInt32 );
		EXPECT_EQ( alt(UInt, jvalue{9}), EValue::UInt32 );
		EXPECT_EQ( alt(ULong, jvalue{10}), EValue::UInt64 );
		for( let& type : {SmallFloat, Float, Decimal, Numeric, Money} )
			EXPECT_EQ( alt(type, jvalue{1.5}), EValue::Double ) << (uint)type;

		//db-review3 #3: an ISO string must become a time point, not stay a string - the drivers bind Time as a datetime and a
		//String as text, so this decides whether the column round-trips at all.
		for( let& type : {DateTime, SmallDateTime} ){
			EXPECT_EQ( alt(type, jvalue{"2026-08-01T00:00:00Z"}), EValue::Time ) << (uint)type;
			EXPECT_EQ( alt(type, jvalue{"$now"}), EValue::String ) << (uint)type; //the one string that survives - the syntax layer turns it into the dialect's now().
		}
		EXPECT_EQ( (Value{DateTime, jvalue{"2026-08-01T00:00:00Z"}, SRCE_CUR}.get_time()), Chrono::ToTimePoint("2026-08-01T00:00:00Z", SRCE_CUR) ); //not just the alternative - the value survives the trip.

		//a json null is Null whatever the column says, before the switch is reached.
		for( let& type : {VarChar, Int, DateTime, Bit} )
			EXPECT_EQ( alt(type, jvalue{}), EValue::Null ) << (uint)type;

		//a guid's text is its 16 bytes in RFC 4122 order, which Value(uuid) holds; anything else is refused.
		let guid = Value{ Guid, jvalue{"0f8fad5b-d9cb-469f-a165-70867728950e"}, SRCE_CUR };
		ASSERT_EQ( guid.Type(), EValue::Bytes );
		EXPECT_EQ( Jde::ToString(guid.get_guid()), "0f8fad5b-d9cb-469f-a165-70867728950e" );
		EXPECT_EQ( guid.get_bytes().front(), 0x0f );
		EXPECT_THROW( (Value{Guid, jvalue{"x"}, SRCE_CUR}), Exception );
		EXPECT_THROW( (Value{Guid, jvalue{"0f8fad5b-d9cb-469f-a165-70867728950"}, SRCE_CUR}), Exception ) << "a digit short";
		EXPECT_THROW( (Value{Guid, jvalue{42}, SRCE_CUR}), std::exception );

		//the unimplemented families report rather than yielding something wrong.
		for( let& type : {Binary, VarBinary, Cursor, RefCursor, Image, Blob, TimeSpan, None} )
			EXPECT_THROW( (Value{type, jvalue{"x"}, SRCE_CUR}), Exception ) << (uint)type;
		for( let& type : {Char, WChar} )
			EXPECT_THROW( (Value{type, jvalue{"x"}, SRCE_CUR}), Exception ) << (uint)type;
	}

	TEST( ValueTests, ToTypeMapsCommonMetaNames ){
		using enum EType;
		const std::pair<sv,EType> exported[]{ //`types:` in libs/db/config/common-meta.libsonnet, in its own order.
			{"Binary",Binary}, {"Bit",Bit}, {"Blob",Blob}, {"Char",Char}, {"DateTime",DateTime}, {"Decimal",Decimal},
			{"Float",Float}, {"Guid",Guid}, {"Image",Image}, {"Int",Int}, {"Int8",Int8}, {"Int16",Int16}, {"Long",Long},
			{"Money",Money}, {"NText",NText}, {"Numeric",Numeric}, {"SmallDateTime",SmallDateTime},
			{"SmallFloat",SmallFloat}, {"Text",Text}, {"UInt",UInt}, {"UInt8",UInt8}, {"UInt16",UInt16}, {"ULong",ULong},
			{"VarBinary",VarBinary}, {"Varchar",VarChar} };
		for( const auto& [name,expected] : exported )
			EXPECT_EQ( ToType(name), expected ) << name; //ToType is case-insensitive (Str::iv), so the exported casing is what matters.

		const std::pair<sv,EType> aliases[]{ {"bool",Bit}, {"uint64",ULong}, {"uri",Uri} }; //ConfigTypeNames' own spellings, which common-meta does not export.
		for( const auto& [name,expected] : aliases )
			EXPECT_EQ( ToType(name), expected ) << name;

		//The four common-meta also exports that deliberately stay unmapped: Cursor/RefCursor are proc constructs rather
		//than column types, TimeSpan has no SQL spelling here, and TChar has no EType at all.  Mapping them would only
		//move the failure into Syntax::ToString, which has no branch for any of them.  The WARN is the guard.
		for( const sv name : {"Cursor"sv, "RefCursor"sv, "TimeSpan"sv, "TChar"sv} )
			EXPECT_EQ( ToType(name), None ) << name;
	}

	TEST( ValueTests, Equality ){
		static_assert( std::equality_comparable<Value> );
		EXPECT_TRUE( Value{5}==Value{5} );
		EXPECT_FALSE( Value{5}==Value{6} );
		EXPECT_TRUE( Value{string{"a"}}==Value{string{"a"}} );
		EXPECT_FALSE( Value{string{"a"}}==Value{string{"b"}} );
		EXPECT_FALSE( Value{5}==Value{string{"5"}} ); //different alternatives never compare equal.
		EXPECT_TRUE( Value{}==Value{} );              //both null.
		EXPECT_FALSE( Value{}==Value{5} );
		EXPECT_TRUE( Value{5}!=Value{6} );            //rewritten from ==.
	}

	TEST( ValueTests, Get ){
		EXPECT_EQ( Value{uint32_t{7}}.Get<uint32_t>(), 7u );
		EXPECT_EQ( Value{42}.Get<_int>(), 42 );          //widening across alternatives still goes through get_number.
		EXPECT_DOUBLE_EQ( Value{1.5}.Get<double>(), 1.5 );
		EXPECT_TRUE( Value{true}.Get<uint32_t>()==1u );
		EXPECT_EQ( Value{string{"abc"}}.Get<string>(), "abc" );

		//A type the value does not hold asserts and yields the default rather than throwing:  these accessors are noexcept,
		//so a throw out of one was a std::terminate with no message.  The ASSERT logs CRITICAL naming the type it does
		//hold, which is the diagnostic - the return is only there so the caller survives a bug it cannot handle anyway.
		EXPECT_EQ( Value{42}.Get<string>(), "" );               //a number is not a string.
		EXPECT_EQ( Value{string{"abc"}}.Get<uint32_t>(), 0u );  //and a string is not a number.
		EXPECT_EQ( Value{}.Get<uint32_t>(), 0u );               //null counts as a mismatch: a nullable column wants Row's *Opt accessors.
		EXPECT_EQ( Value{}.Get<string>(), "" );
	}

	//db-refactor B8: the reference a mismatched get_string/get_bytes hands back was one process-wide static per type, so a caller that
	//wrote through it changed what every later mismatch read.  It is thread-local and cleared on every hand-out now.
	TEST( ValueTests, HeapFallbackIsFresh ){
		Value v{ 42 };
		v.get_string() = "poison"; //writes through the mismatch fallback (the ASSERT is log-only).
		EXPECT_TRUE( Value{1}.get_string().empty() );          //the next mismatch does not see it.
		EXPECT_TRUE( Value{string{"x"}}.get_bytes().empty() ); //nor the other alternative's.
		EXPECT_EQ( Value{string{"x"}}.get_string(), "x" );      //a match is the cell itself, as before.
	}

	//db-review2 #21: Value::ToUInt on a negative Double must not be UB - go through signed _int, matching the integer cases' modular wrap.
	TEST( ValueTests, ToUInt ){
		EXPECT_EQ( Value{-5.0}.ToUInt(), (uint)(_int)-5 ); //modular wrap, deterministic (was UB).
		EXPECT_EQ( Value{-5.0}.ToInt(), -5 );              //ToInt = (_int)ToUInt round-trips.
		EXPECT_EQ( Value{42.0}.ToUInt(), 42u );            //non-negative unchanged.
		EXPECT_EQ( Value{string{"x"}}.ToUInt(), 0u ); //non-numeric: 0 (and a WARN); null: 0 silently.
		EXPECT_EQ( Value{}.ToUInt(), 0u );
		EXPECT_EQ( Value{true}.ToUInt(), 1u );
	}

	//db-refactor A2: ToString/ToJson/ToUInt/get_number are visits now, so an alternative missing from one is a compile
	//error - this test and the next pin the *spelling* each produces, which the visit cannot.
	TEST( ValueTests, ToStringCoversEveryAlternative ){
		let now = DBTimePoint{ std::chrono::floor<std::chrono::seconds>(DBClock::now()) };
		const vector<uint8_t> bytes{ 1, 2, 3 };
		EXPECT_EQ( Value{}.ToString(), "null" );
		EXPECT_EQ( Value{string{"abc"}}.ToString(), "abc" );
		EXPECT_EQ( Value{true}.ToString(), "true" );
		EXPECT_EQ( Value{false}.ToString(), "false" );
		EXPECT_EQ( Value{(int8_t)-3}.ToString(), "-3" );
		EXPECT_EQ( Value{42}.ToString(), "42" );
		EXPECT_EQ( Value{_int{-9}}.ToString(), "-9" );
		EXPECT_EQ( Value{uint32_t{7}}.ToString(), "7" );
		EXPECT_EQ( Value{uint{9}}.ToString(), "9" );
		EXPECT_EQ( Value{1.5}.ToString(), std::to_string(1.5) );
		EXPECT_EQ( Value{now}.ToString(), ToIsoString(now) );
		EXPECT_EQ( Value{bytes}.ToString(), Str::Encode64(bytes) );
	}

	TEST( ValueTests, ToJsonCoversEveryAlternative ){
		let now = DBTimePoint{ std::chrono::floor<std::chrono::seconds>(DBClock::now()) };
		const vector<uint8_t> bytes{ 1, 2, 3 };
		EXPECT_EQ( Value{uint32_t{7}}.ToJson().to_number<uint32_t>(), 7u );  //the gap.
		EXPECT_FALSE( Value{uint32_t{7}}.ToJson().is_null() );
		EXPECT_EQ( Value{uint32_t{0}}.ToJson().to_number<uint32_t>(), 0u );  //0 is not null either.

		EXPECT_TRUE( Value{}.ToJson().is_null() );
		EXPECT_EQ( Value{string{"abc"}}.ToJson().as_string(), "abc" );
		EXPECT_TRUE( Value{true}.ToJson().as_bool() );
		EXPECT_EQ( Value{(int8_t)-3}.ToJson().to_number<int>(), -3 );
		EXPECT_EQ( Value{42}.ToJson().to_number<int>(), 42 );
		EXPECT_EQ( Value{uint{9}}.ToJson().to_number<uint>(), 9u );
		EXPECT_EQ( Value{_int{-9}}.ToJson().to_number<_int>(), -9 );
		EXPECT_DOUBLE_EQ( Value{1.5}.ToJson().to_number<double>(), 1.5 );
		EXPECT_EQ( Value{now}.ToJson().as_string(), ToIsoString(now) );
		EXPECT_EQ( Value{bytes}.ToJson().as_string(), Str::Encode64(bytes) );
	}
}
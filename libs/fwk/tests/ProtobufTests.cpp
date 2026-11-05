#include <jde/fwk/io/protobuf.h>

#define let const auto

namespace Jde::Tests{
	//ToGuid copies at most 16 bytes, and no more than it is given:  an id field unset (proto3's "") or from a foreign writer
	//is short, a foreign one may be long.  Without the clamp a short input over-reads and a long one overflows the uuid -
	//both ASan aborts here.  The literal's ascending bytes pin the order too.
	TEST( ProtobufTests, ToGuidClampsToTheInputLength ){
		let g = Protobuf::ToGuid( "abc" );
		EXPECT_EQ( sv((const char*)g.data(), 3), "abc" );
		EXPECT_TRUE( std::all_of(g.begin()+3, g.end(), [](auto b){ return b==0; }) ) << "the tail past a short input is zero";
		EXPECT_EQ( Protobuf::ToGuid(""), uuid{} );
		let full = uuid{ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15} };
		EXPECT_EQ( Protobuf::ToGuid(string{Protobuf::ToBytes(full)}+"xx"), full ) << "bytes past 16 are ignored";
	}
	//A 0 or END_GROUP tag ends a parse early; the buffer must still be read to its end or the parse fails.
	TEST( ProtobufTests, DeserializeRejectsAnEarlyEnd ){
		using google::protobuf::Timestamp;
		Timestamp t; t.set_seconds( 1 ); t.set_nanos( 2 );
		let bytes = t.SerializeAsString();
		ASSERT_EQ( bytes, string("\x08\x01\x10\x02", 4) );
		EXPECT_EQ( Protobuf::Deserialize<Timestamp>(string{bytes}).nanos(), 2 );
		EXPECT_THROW( Protobuf::Deserialize<Timestamp>(string("\x08\x01\x00\x10\x02", 5)), Exception ) << "a 0 tag before the last field";
		EXPECT_THROW( Protobuf::Deserialize<Timestamp>(bytes+'\0'), Exception ) << "a trailing 0 tag";
		EXPECT_THROW( Protobuf::Deserialize<Timestamp>(bytes+'\x0c'), Exception ) << "an END_GROUP tag";
	}

	TEST( ProtobufTests, ToTimestampFloorsBefore1970 ){
		let before = TimePoint{}-std::chrono::milliseconds{ 500 };
		let t = Protobuf::ToTimestamp( before );
		EXPECT_EQ( t.seconds(), -1 );
		EXPECT_EQ( t.nanos(), 500'000'000 ) << "a Timestamp's nanos are in [0,1e9)";
		EXPECT_EQ( Protobuf::ToTimePoint(t), before );
		let now = Clock::now();
		EXPECT_EQ( Protobuf::ToTimePoint(Protobuf::ToTimestamp(now)), now );
	}
	TEST( ProtobufTests, ToTimestampTakesEveryTimePoint ){
		for( let tp : {TimePoint::min(), TimePoint::max()} ){
			let t = Protobuf::ToTimestamp( tp );
			EXPECT_EQ( t.seconds(), std::chrono::floor<std::chrono::seconds>(tp.time_since_epoch()).count() );
			EXPECT_TRUE( t.nanos()>=0 && t.nanos()<1'000'000'000 ) << t.nanos();
		}
	}
	TEST( ProtobufTests, ToTimePointSaturates ){
		using Limits = std::numeric_limits<int64_t>;
		google::protobuf::Timestamp t;
		t.set_seconds( (Limits::max)() );
		let high = Protobuf::ToTimePoint( t );
		EXPECT_GT( high, Clock::now() ) << "INT64_MAX seconds wrapped instead of saturating";
		EXPECT_EQ( high, Protobuf::ToTimePoint(Protobuf::MaxSeconds, 0) );
		let underASecond = duration_cast<Duration>( std::chrono::nanoseconds{999'999'999} );
		EXPECT_EQ( Protobuf::ToTimePoint(0, (Limits::max)()), TimePoint{}+underASecond );
		EXPECT_EQ( Protobuf::ToTimePoint(0, (Limits::min)()), TimePoint{}-underASecond );
		t.set_nanos( 999'999'999 );
		EXPECT_GE( Protobuf::ToTimePoint(t), high ) << "nanos on top of the bound";
		t.set_seconds( (Limits::min)() ); t.set_nanos( 0 );
		EXPECT_LT( Protobuf::ToTimePoint(t), TimePoint{} ) << "INT64_MIN seconds wrapped instead of saturating";
	}
}
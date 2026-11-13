//The message (docs/flows/spec.md *Messages*):  its ids, a property's path, and a data change's fields, a 64-bit integer an integer.
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	TEST( Message, IdsNeverRepeatAcrossRuns ){
		MessageIds first{ At(6) }, second{ At(6, 0, 1) };
		let a = first.Next(); let b = first.Next();
		EXPECT_EQ( b, a+1 );
		EXPECT_EQ( a>>32, (MessageId)duration_cast<seconds>(At(6).time_since_epoch()).count() ) << "the run's start in the high half";
		EXPECT_EQ( a&0xFFFFFFFF, 1u );
		EXPECT_GT( second.Next(), first.Next()+0xFFFFFFF0 ) << "a second later starts past four billion of the first's";
	}

	//#451 review #12:  an id goes out as its json's `_id` reads back and is rendered, before 2038 and after, when ids pass 2^63.
	TEST( Message, AnIdGoesOutAsItsJsonReadsBack ){
		for( let id : {MessageIds{At(6)}.Next(), MessageIds{sys_days{2040y/January/1}}.Next()} ){
			auto json = parse( serialize(jobject{{"_id", id}}) );
			ToWire( json );
			EXPECT_EQ( json.at("_id"), jvalue{ToLong(id)} ) << id;
			jvalue held{ jobject{{"_id", id}} };//as the engine holds it, never text:  the live push's.
			ToWire( held );
			EXPECT_EQ( held.at("_id"), jvalue{ToLong(id)} ) << id;
			EXPECT_EQ( FromLong(ToLong(id)).to_number<MessageId>(), id ) << id;
		}
	}

	TEST( Message, Fields ){
		Message m{ jobject{{"_id", 7}, {"_parent", 3}, {"topic", "pump1"}, {"payload", 4.5}} };
		EXPECT_EQ( m.Id(), 7u );
		EXPECT_EQ( m.Parent(), 3u );
		EXPECT_EQ( m.Topic(), "pump1" );
		EXPECT_EQ( m.Payload(), 4.5 );
		Message empty;
		EXPECT_EQ( empty.Id(), 0u );
		EXPECT_EQ( empty.Parent(), 0u );
		EXPECT_EQ( empty.Topic(), "" );
		EXPECT_TRUE( empty.Payload().is_null() );
		Message big{ jobject{{"_id", std::numeric_limits<uint64_t>::max()}} };
		EXPECT_EQ( big.Id(), std::numeric_limits<uint64_t>::max() );
	}

	TEST( Message, Paths ){
		auto o = parse( R"({"payload":{"speed":12,"rows":[{"id":1},{"id":2}]},"topic":"t"})" ).get_object();
		EXPECT_EQ( *Path{"topic"}.Find(o), "t" );
		EXPECT_EQ( *Path{"payload.speed"}.Find(o), 12 );
		EXPECT_EQ( *Path{"payload.rows[1].id"}.Find(o), 2 );
		EXPECT_FALSE( Path{"payload.rows[2].id"}.Find(o) );
		EXPECT_FALSE( Path{"payload.speed.unit"}.Find(o) );
		EXPECT_FALSE( Path{"absent"}.Find(o) );
		EXPECT_EQ( Path{"payload.rows[1].id"}.Root(), "payload" );

		Path{"payload.rows[1].id"}.Set( o, 9 );
		Path{"payload.rows[2]"}.Set( o, "appended" );
		Path{"a.b.c"}.Set( o, true );
		Path{"topic"}.Set( o, "u" );
		EXPECT_EQ( serialize(o), R"({"payload":{"speed":12,"rows":[{"id":1},{"id":9},"appended"]},"topic":"u","a":{"b":{"c":true}}})" );
		EXPECT_THROW( Path{"topic.inner"}.Set(o, 1), Exception ) << "through text";
		EXPECT_THROW( Path{"payload.rows[5]"}.Set(o, 1), Exception ) << "past the array's end";

		EXPECT_TRUE( Path{"payload.rows[0]"}.Erase(o) );
		EXPECT_TRUE( Path{"a.b"}.Erase(o) );
		EXPECT_TRUE( Path{"topic"}.Erase(o) );
		EXPECT_FALSE( Path{"topic"}.Erase(o) );
		EXPECT_FALSE( Path{"payload.rows[7]"}.Erase(o) );
		EXPECT_EQ( serialize(o), R"({"payload":{"speed":12,"rows":[{"id":9},"appended"]},"a":{}})" );

		for( let bad : {"", ".", "a.", "a..b", "[0]", "a[x]", "a[1", "a[]", "a[0]b"} )
			EXPECT_THROW( Path{bad}, Exception ) << bad;
	}

	TEST( Message, DataChange ){
		Value count{ jvalue{9007199254740993}, &UA_TYPES[UA_TYPES_INT64] };//2^53+1:  no double holds it.
		count.sourceTimestamp = UADateTime{ At(6, 0, 1) }.UA();
		count.hasSourceTimestamp = true;
		count.serverTimestamp = UADateTime{ At(6, 0, 2) }.UA();
		count.hasServerTimestamp = true;
		Message m;
		m.SetDataChange( "plc1", "ns=2;s=Count", count );
		ASSERT_TRUE( m.Payload().is_int64() ) << serialize( m.Payload() );
		EXPECT_EQ( m.Payload().get_int64(), 9007199254740993 );
		EXPECT_EQ( serialize(m.Json.at("node")), R"({"connection":"plc1","id":"ns=2;s=Count"})" );
		EXPECT_EQ( m.Json.at("status"), 0 );
		EXPECT_EQ( m.Json.at("sourceTime"), Iso(At(6, 0, 1)) );
		EXPECT_EQ( m.Json.at("serverTime"), Iso(At(6, 0, 2)) );

		let top = ToPayload( Value{jvalue{std::numeric_limits<uint64_t>::max()}, &UA_TYPES[UA_TYPES_UINT64]} );
		ASSERT_TRUE( top.is_uint64() ) << serialize( top );
		EXPECT_EQ( top.get_uint64(), std::numeric_limits<uint64_t>::max() );
		EXPECT_EQ( serialize(ToPayload(Value{jarray{-1, 2}, &UA_TYPES[UA_TYPES_INT64]})), "[-1,2]" );
		EXPECT_EQ( ToPayload(Value{jvalue{21.5}, &UA_TYPES[UA_TYPES_DOUBLE]}), 21.5 );
		EXPECT_EQ( ToPayload(Value{jvalue{"running"}, &UA_TYPES[UA_TYPES_STRING]}), "running" );
		EXPECT_EQ( ToPayload(Value{jvalue{true}, &UA_TYPES[UA_TYPES_BOOLEAN]}), true );

		Message lost;
		lost.SetDataChange( "plc1", "ns=2;s=Count", Value{UA_STATUSCODE_BADNOCOMMUNICATION} );
		EXPECT_TRUE( lost.Payload().is_null() );
		EXPECT_EQ( lost.Json.at("status"), UA_STATUSCODE_BADNOCOMMUNICATION );
		EXPECT_FALSE( lost.Json.contains("sourceTime") );
	}
}
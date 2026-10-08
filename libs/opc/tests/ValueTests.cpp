//Value is the read path (UA_DataValue -> json for the gateway's clients) and the write path (json -> UA_DataValue).
//Whatever ToJson emits for a type, Set has to accept back.
#include <cmath>
#include <limits>
#include <gtest/gtest.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/uatypes/Variant.h>
#include <jde/opc/proto/opc.Common.h>

#define let const auto

namespace Jde::Opc::Tests{
	Ω dataValue( const void* value, const UA_DataType& type )ι->Value{
		UA_DataValue dv{};
		UA_Variant_setScalarCopy( &dv.value, value, &type );
		dv.hasValue = true;
		return Value{ move(dv) };
	}
	Ω arrayDataValue( const void* values, uint count, const UA_DataType& type )ι->Value{
		UA_DataValue dv{};
		UA_Variant_setArrayCopy( &dv.value, values, count, &type );
		dv.hasValue = true;
		return Value{ move(dv) };
	}

	TEST( ValueTests, EmptyAndStatus ){
		EXPECT_TRUE( Value( UA_DataValue{} ).IsEmpty() );
		EXPECT_TRUE( Value( UA_DataValue{} ).ToJson().is_null() );

		Value bad{ (StatusCode)UA_STATUSCODE_BADNODEIDUNKNOWN };
		let j = bad.ToJson();
		EXPECT_EQ( j.at("sc").to_number<uint32_t>(), UA_STATUSCODE_BADNODEIDUNKNOWN );
	}

	TEST( ValueTests, ScalarToJson ){
		const UA_Boolean b{ true };
		EXPECT_TRUE( dataValue(&b, UA_TYPES[UA_TYPES_BOOLEAN]).ToJson().as_bool() );

		const UA_Int16 i16{ -3 };
		EXPECT_EQ( dataValue(&i16, UA_TYPES[UA_TYPES_INT16]).ToJson().to_number<int>(), -3 );

		const UA_Int32 i32{ 42 };
		EXPECT_EQ( dataValue(&i32, UA_TYPES[UA_TYPES_INT32]).ToJson().to_number<int>(), 42 );

		const UA_UInt32 u32{ 42 };
		EXPECT_EQ( dataValue(&u32, UA_TYPES[UA_TYPES_UINT32]).ToJson().to_number<uint32_t>(), 42u );

		const UA_Double d{ 1.5 };
		EXPECT_DOUBLE_EQ( dataValue(&d, UA_TYPES[UA_TYPES_DOUBLE]).ToJson().to_number<double>(), 1.5 );

		const UA_Float f{ 1.5f };
		EXPECT_DOUBLE_EQ( dataValue(&f, UA_TYPES[UA_TYPES_FLOAT]).ToJson().to_number<double>(), 1.5 );

		const UA_Byte byte{ 200 };
		EXPECT_EQ( dataValue(&byte, UA_TYPES[UA_TYPES_BYTE]).ToJson().to_number<int>(), 200 );

		const UA_SByte sbyte{ -100 };
		EXPECT_EQ( dataValue(&sbyte, UA_TYPES[UA_TYPES_SBYTE]).ToJson().to_number<int>(), -100 );

		let text = ToUV( "abc" );
		EXPECT_EQ( dataValue(&text, UA_TYPES[UA_TYPES_STRING]).ToJson().as_string(), "abc" );

		const UA_StatusCode sc{ UA_STATUSCODE_GOODCLAMPED };
		EXPECT_EQ( dataValue(&sc, UA_TYPES[UA_TYPES_STATUSCODE]).ToJson().to_number<uint32_t>(), UA_STATUSCODE_GOODCLAMPED );
	}

	//The 64-bit integers use the protobufjs Long form so javascript clients keep full precision.
	//A Float wrote its widened double, 21.3f as 21.299999237060547, which the History table and the Children tab showed
	//(historian-web-trend, Tooltip decimals).  It writes the float's shortest, which narrows back to the same float.
	TEST( ValueTests, AFloatWritesItsShortest ){
		const UA_Float f{ 21.3f };
		EXPECT_EQ( dataValue(&f, UA_TYPES[UA_TYPES_FLOAT]).ToJson().to_number<double>(), 21.3 );
		for( const UA_Float x : {21.3f, 0.000123f, 16777216.f, -1.17549435e-38f, std::numeric_limits<UA_Float>::max(), std::numeric_limits<UA_Float>::denorm_min()} )
			EXPECT_EQ( (UA_Float)dataValue(&x, UA_TYPES[UA_TYPES_FLOAT]).ToJson().to_number<double>(), x );
	}
	TEST( ValueTests, SixtyFourBitIntegersToJson ){
		const UA_Int64 i64{ 0x0000000100000002ll };
		let j = dataValue( &i64, UA_TYPES[UA_TYPES_INT64] ).ToJson();
		EXPECT_EQ( j.at("high").to_number<uint32_t>(), 1u );
		EXPECT_EQ( j.at("low").to_number<uint32_t>(), 2u );
		EXPECT_FALSE( j.at("unsigned").as_bool() );

		const UA_UInt64 u64{ 5 };
		EXPECT_TRUE( dataValue(&u64, UA_TYPES[UA_TYPES_UINT64]).ToJson().at("unsigned").as_bool() );
	}

	TEST( ValueTests, NodeIdAndGuidToJson ){
		let n = NodeId{ 2, 5002 };
		let j = dataValue( static_cast<const UA_NodeId*>(&n), UA_TYPES[UA_TYPES_NODEID] ).ToJson();
		EXPECT_EQ( j.at("ns").to_number<int>(), 2 );
		EXPECT_EQ( j.at("i").to_number<int>(), 5002 );

		constexpr UA_Guid guid{ 0x12345678, 0x1234, 0x5678, {0x12,0x34,0x56,0x78,0x12,0x34,0x56,0x78} };
		EXPECT_EQ( dataValue(&guid, UA_TYPES[UA_TYPES_GUID]).ToJson().as_string(), "12345678-1234-5678-1234-567812345678" );
	}

	TEST( ValueTests, ArrayToJson ){
		const UA_Int32 values[]{ 1, 2, 3 };
		EXPECT_EQ( serialize(arrayDataValue(values, 3u, UA_TYPES[UA_TYPES_INT32]).ToJson()), "[1,2,3]" );

		const UA_Double doubles[]{ 1.5, 2.5 };
		let asJson = arrayDataValue( doubles, 2u, UA_TYPES[UA_TYPES_DOUBLE] ).ToJson();
		ASSERT_EQ( asJson.as_array().size(), 2u );
		EXPECT_DOUBLE_EQ( asJson.as_array()[0].to_number<double>(), 1.5 );
		EXPECT_DOUBLE_EQ( asJson.as_array()[1].to_number<double>(), 2.5 );
	}

	//Set -> ToJson has to be the identity for every type the write path claims to support.
	TEST( ValueTests, SetRoundTrip ){
		EXPECT_TRUE( Value( jvalue{true}, &UA_TYPES[UA_TYPES_BOOLEAN] ).ToJson().as_bool() );
		EXPECT_EQ( Value( jvalue{42}, &UA_TYPES[UA_TYPES_INT16] ).ToJson().to_number<int>(), 42 );
		EXPECT_EQ( Value( jvalue{42}, &UA_TYPES[UA_TYPES_INT32] ).ToJson().to_number<int>(), 42 );
		EXPECT_EQ( Value( jvalue{42}, &UA_TYPES[UA_TYPES_UINT32] ).ToJson().to_number<uint32_t>(), 42u );
		EXPECT_EQ( Value( jvalue{200}, &UA_TYPES[UA_TYPES_BYTE] ).ToJson().to_number<int>(), 200 );
		EXPECT_DOUBLE_EQ( Value( jvalue{1.5}, &UA_TYPES[UA_TYPES_DOUBLE] ).ToJson().to_number<double>(), 1.5 );
		EXPECT_DOUBLE_EQ( Value( jvalue{1.5}, &UA_TYPES[UA_TYPES_FLOAT] ).ToJson().to_number<double>(), 1.5 );
		EXPECT_EQ( Value( jvalue{"abc"}, &UA_TYPES[UA_TYPES_STRING] ).ToJson().as_string(), "abc" );
	}

	//review3 #6: ToJson emits a 64-bit integer as the protobufjs Long {high,low,unsigned} so the SPA can build a Long,
	//but Set routed INT64/UINT64 to AsNumber, whose try_to_number returns not_number for an object - so the library
	//rejected its own output with "Could not convert to number.".  Any read-modify-write client trips it, and so does a
	//`GatewayService.write( …, Long )`: long.js 5.3.2 has no toJSON, so JSON.stringify sends {low,high,unsigned}.
	TEST( ValueTests, SetAcceptsTheLongFormItEmits ){
		for( let v : { (UA_Int64)0, (UA_Int64)42, (UA_Int64)-42, (UA_Int64)0x0000000100000002ll,
				(std::numeric_limits<UA_Int64>::max)(), (std::numeric_limits<UA_Int64>::min)() } ){
			let j = dataValue( &v, UA_TYPES[UA_TYPES_INT64] ).ToJson();
			ASSERT_TRUE( j.is_object() ) << v;
			let round = Value{ j, &UA_TYPES[UA_TYPES_INT64] };
			EXPECT_EQ( round.Get<UA_Int64>(0), v ) << serialize( j );
			EXPECT_EQ( serialize(round.ToJson()), serialize(j) ) << v;//and it re-emits the same shape.
		}
		for( let v : { (UA_UInt64)0, (UA_UInt64)5, (std::numeric_limits<UA_UInt64>::max)() } ){
			let j = dataValue( &v, UA_TYPES[UA_TYPES_UINT64] ).ToJson();
			EXPECT_EQ( Value( j, &UA_TYPES[UA_TYPES_UINT64] ).Get<UA_UInt64>(0), v ) << serialize( j );
		}
		//protobufjs's own spelling - a *signed* int32 low - reads the same, which is the point of masking each half.
		EXPECT_EQ( Value( parse(R"({"high":0,"low":-1,"unsigned":true})"), &UA_TYPES[UA_TYPES_UINT64] ).Get<UA_UInt64>(0), 0xFFFFFFFFull );
		//A typeless write takes its signedness from the flag, so the inference sees a number rather than an object.
		EXPECT_TRUE( Value( parse(R"({"high":0,"low":5,"unsigned":true})"), nullptr ).ToJson().at("unsigned").as_bool() );
		EXPECT_FALSE( Value( parse(R"({"high":0,"low":5,"unsigned":false})"), nullptr ).ToJson().at("unsigned").as_bool() );
		//A half that is not a 32-bit half is malformed, not truncated.
		EXPECT_THROW( Value( parse(R"({"high":4294967296,"low":0})"), &UA_TYPES[UA_TYPES_INT64] ), Exception );
	}

	//review3 #7: `IS(ua)` compared the descriptor *address*, so the NS0 alias descriptors FindDataType returns -
	//IntegerId (i=288), Counter, LocaleId, Image*, AudioData … - matched no arm at all, although each carries the same
	//typeKind as the type it aliases.  A write to such a node threw "has not been implemented", a read was "not a
	//number", and OpcServer's {dataType:288,value:5} could not create the node.  Dispatch is on typeKind now.
	TEST( ValueTests, Ns0AliasTypesTakeTheirBaseArm ){
		auto integerId = Value{ jvalue{5}, &UA_TYPES[UA_TYPES_INTEGERID] };//UInt32 by kind.
		EXPECT_EQ( integerId.ToJson().to_number<uint32_t>(), 5u );
		EXPECT_EQ( integerId.AsNumber<int>(), 5 );

		const UA_UInt32 counter{ 7 };
		auto asCounter = dataValue( &counter, UA_TYPES[UA_TYPES_COUNTER] );
		EXPECT_EQ( asCounter.ToJson().to_number<uint32_t>(), 7u );
		EXPECT_EQ( asCounter.AsNumber<uint>(), 7u );

		EXPECT_EQ( Value( jvalue{"tag"}, &UA_TYPES[UA_TYPES_LOCALEID] ).ToJson().as_string(), "tag" );//String by kind.
		EXPECT_DOUBLE_EQ( Value( jvalue{1.5}, &UA_TYPES[UA_TYPES_DURATION] ).AsNumber<double>(), 1.5 );//Double by kind.
		//UtcTime is a DateTime by kind, so it reads the same json a DateTime does.
		EXPECT_EQ( Value( parse(R"({"seconds":1700000000,"nanos":0})"), &UA_TYPES[UA_TYPES_UTCTIME] ).ToJson().at("seconds").to_number<int64_t>(), 1700000000 );
	}

	//Same finding: Set had a BYTE arm but no SBYTE one, so every write to an SByte node - i=2, what a PLC server gives a
	//`Char`, and what the SPA classes isInteger and sends as a plain number - threw "has not been implemented".
	TEST( ValueTests, SetAcceptsAnSByte ){
		EXPECT_EQ( Value( jvalue{-100}, &UA_TYPES[UA_TYPES_SBYTE] ).Get<UA_SByte>(0), -100 );
		EXPECT_EQ( Value( jvalue{127}, &UA_TYPES[UA_TYPES_SBYTE] ).AsNumber<int>(), 127 );
		EXPECT_THROW( Value( jvalue{128}, &UA_TYPES[UA_TYPES_SBYTE] ), Exception );//still range-checked.
	}

	//Same finding: Set had no array branch at all, so an Int32[3] write-back failed with the misleading
	//"'[1,2,3]', Could not convert to number.".  The contract in this file's header is what is pinned - whatever ToJson
	//emits for a type, Set has to accept back - now for the array shape as well as the scalar one.
	TEST( ValueTests, ArrayRoundTrip ){
		const UA_Int32 i32[]{ 1, -2, 3 };
		let ints = arrayDataValue( i32, 3u, UA_TYPES[UA_TYPES_INT32] );
		let j = ints.ToJson();
		ASSERT_TRUE( j.is_array() );
		let round = Value{ j, &UA_TYPES[UA_TYPES_INT32] };
		EXPECT_FALSE( round.IsScalar() );
		ASSERT_EQ( round.value.arrayLength, 3u );
		EXPECT_EQ( round.Get<UA_Int32>(2), 3 );
		EXPECT_EQ( serialize(round.ToJson()), serialize(j) );

		//The elements go through Set, so the array shape inherits every scalar arm - including #6's Long form...
		const UA_Int64 i64[]{ 42, -42 };
		let longs = arrayDataValue( i64, 2u, UA_TYPES[UA_TYPES_INT64] );
		let longsRound = Value{ longs.ToJson(), &UA_TYPES[UA_TYPES_INT64] };
		EXPECT_EQ( longsRound.Get<UA_Int64>(1), -42 );
		//...and the heap types, whose elements the array now owns.
		let strings = { ToUV("a"), ToUV("bc") };
		let text = arrayDataValue( std::data(strings), 2u, UA_TYPES[UA_TYPES_STRING] );
		let textRound = Value{ text.ToJson(), &UA_TYPES[UA_TYPES_STRING] };
		ASSERT_EQ( textRound.value.arrayLength, 2u );
		EXPECT_EQ( ToString(textRound.Get<UA_String>(1)), "bc" );

		EXPECT_EQ( Value( parse("[]"), &UA_TYPES[UA_TYPES_INT32] ).value.arrayLength, 0u );//an empty array is still an array.
		EXPECT_THROW( Value( parse("[[1,2]]"), &UA_TYPES[UA_TYPES_INT32] ), Exception );   //a UA array is flat.
		EXPECT_THROW( Value( parse(R"([1,"x"])"), &UA_TYPES[UA_TYPES_INT32] ), Exception );//one bad element fails the write.
	}

	//review3 #10: a NaN reading serialized as `null` - Boost.JSON's default serialize_options - which is the same text a
	//value-less reading produces, so a failed sensor reached the SPA as a blank tag, while the same node over the socket
	//showed NaN.  The alternative the finding offered, allow_infinity_and_nan, writes the bare tokens NaN/Infinity, which
	//are not json: JSON.parse throws on the whole response.  OPC-UA Part 6 spells them as strings, which is what
	//UA_encodeJson writes and therefore what Variant::ToJson has always emitted - so Value now agrees with Variant.
	TEST( ValueTests, NonFiniteReadingsKeepTheirUaSpelling ){
		let nan = std::numeric_limits<UA_Double>::quiet_NaN();
		let inf = std::numeric_limits<UA_Double>::infinity();
		let negInf = -inf;
		EXPECT_EQ( serialize(dataValue(&nan, UA_TYPES[UA_TYPES_DOUBLE]).ToJson()), R"("NaN")" );
		EXPECT_EQ( serialize(dataValue(&inf, UA_TYPES[UA_TYPES_DOUBLE]).ToJson()), R"("Infinity")" );
		EXPECT_EQ( serialize(dataValue(&negInf, UA_TYPES[UA_TYPES_DOUBLE]).ToJson()), R"("-Infinity")" );
		let nanF = std::numeric_limits<UA_Float>::quiet_NaN();
		EXPECT_EQ( serialize(dataValue(&nanF, UA_TYPES[UA_TYPES_FLOAT]).ToJson()), R"("NaN")" );

		//The point of the finding: it is no longer the same text as a reading that never happened.
		EXPECT_NE( serialize(dataValue(&nan, UA_TYPES[UA_TYPES_DOUBLE]).ToJson()), serialize(Value(UA_DataValue{}).ToJson()) );
		//...and a finite reading is untouched.
		const UA_Double finite{ 42.7 };
		EXPECT_DOUBLE_EQ( dataValue(&finite, UA_TYPES[UA_TYPES_DOUBLE]).ToJson().to_number<double>(), 42.7 );

		//Arrays carry it per element, mixed with finite ones.
		const UA_Double mixed[]{ 1.5, nan };
		let arrayJson = arrayDataValue( mixed, 2u, UA_TYPES[UA_TYPES_DOUBLE] ).ToJson();
		ASSERT_TRUE( arrayJson.is_array() );
		EXPECT_EQ( arrayJson.as_array()[1].as_string(), "NaN" );

		//And Set reads every one of them back - the contract in this file's header.
		EXPECT_TRUE( std::isnan(Value( jvalue{"NaN"}, &UA_TYPES[UA_TYPES_DOUBLE] ).Get<UA_Double>(0)) );
		EXPECT_EQ( Value( jvalue{"Infinity"}, &UA_TYPES[UA_TYPES_DOUBLE] ).Get<UA_Double>(0), inf );
		EXPECT_EQ( Value( jvalue{"-Infinity"}, &UA_TYPES[UA_TYPES_DOUBLE] ).Get<UA_Double>(0), negInf );
		EXPECT_TRUE( std::isnan(Value( jvalue{"NaN"}, &UA_TYPES[UA_TYPES_FLOAT] ).Get<UA_Float>(0)) );
		EXPECT_TRUE( std::isnan(Value( arrayJson, &UA_TYPES[UA_TYPES_DOUBLE] ).Get<UA_Double>(1)) );
		//Any other string is still an error rather than a silent NaN.
		EXPECT_THROW( Value( jvalue{"nan"}, &UA_TYPES[UA_TYPES_DOUBLE] ), Exception );
		EXPECT_THROW( Value( jvalue{"abc"}, &UA_TYPES[UA_TYPES_FLOAT] ), Exception );
	}

	//L19: the json constructor never set hasValue.  Harmless while WriteAwait passes only `&_value.value` and the vendor
	//sets the flag itself, but a caller handing over the whole UA_DataValue - which is what the type is - would have sent
	//a value-less write, and the server would have taken it as "no value supplied".
	TEST( ValueTests, TheJsonCtorSaysItHasAValue ){
		let scalar = Value{ jvalue{42}, &UA_TYPES[UA_TYPES_INT32] };
		EXPECT_TRUE( scalar.hasValue );
		EXPECT_FALSE( scalar.IsEmpty() );
		EXPECT_TRUE( Value( parse("[1,2]"), &UA_TYPES[UA_TYPES_INT32] ).hasValue );//the array branch too.
		EXPECT_TRUE( Value( jvalue{5}, nullptr ).hasValue );                       //and the typeless inference.
		//A status-only Value still has none - there is nothing to write.
		EXPECT_FALSE( Value( (StatusCode)UA_STATUSCODE_BADNODEIDUNKNOWN ).hasValue );
	}

	TEST( ValueTests, SetRejectsTheWrongJsonKind ){
		EXPECT_THROW( Value( jvalue{42}, &UA_TYPES[UA_TYPES_BOOLEAN] ), Exception );
		EXPECT_THROW( Value( jvalue{42}, &UA_TYPES[UA_TYPES_STRING] ), Exception );
		EXPECT_THROW( Value( jvalue{nullptr}, nullptr ), Exception );                     //no type and no value to infer from.
		EXPECT_THROW( Value( jvalue{5}, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] ), Exception ); //not implemented, and says so.
	}

	//A typeless Value infers the UA type from the json kind - the path a write takes before the node's DataType is known.
	TEST( ValueTests, TypelessSetInfers ){
		EXPECT_FALSE( Value( jvalue{5}, nullptr ).ToJson().at("unsigned").as_bool() ); //int64 -> the Long form.
		EXPECT_EQ( Value( jvalue{"abc"}, nullptr ).ToJson().as_string(), "abc" );
		EXPECT_DOUBLE_EQ( Value( jvalue{1.5}, nullptr ).ToJson().to_number<double>(), 1.5 );
	}

	TEST( ValueTests, AsNumberOnThirtyTwoBitScalars ){
		const UA_Int32 i32{ 42 };
		EXPECT_EQ( dataValue(&i32, UA_TYPES[UA_TYPES_INT32]).AsNumber<int32_t>(), 42 );

		const UA_Double d{ 42.5 };
		EXPECT_DOUBLE_EQ( dataValue(&d, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<double>(), 42.5 );

		const UA_Int32 values[]{ 1, 2 };
		EXPECT_THROW( arrayDataValue(values, 2u, UA_TYPES[UA_TYPES_INT32]).AsNumber<int32_t>(), Exception );
	}


	//#3 (fixed): the ToJson fallback sat *inside* the per-element loop and re-encoded the whole variant each time, and the
	//IS() chain had no LOCALIZEDTEXT/QUALIFIEDNAME arm - a LocalizedText[3] serialized as [[a,b,c],[a,b,c],[a,b,c]].
	TEST( ValueTests, LocalizedTextArraySerializesPerElement ){
		UA_LocalizedText texts[3]{};
		for( uint i=0; i<3; ++i ){
			texts[i].locale = UA_STRING_NULL;
			texts[i].text = AllocUAString( string(1, (char)('a'+i)) );
		}
		let v = arrayDataValue( texts, 3u, UA_TYPES[UA_TYPES_LOCALIZEDTEXT] );
		for( auto& t : texts )
			UA_LocalizedText_clear( &t );
		EXPECT_EQ( serialize(v.ToJson()), R"(["a","b","c"])" );

		UA_LocalizedText one{};
		one.locale = UA_STRING_NULL;
		one.text = AllocUAString( string{"solo"} );
		let scalar = dataValue( &one, UA_TYPES[UA_TYPES_LOCALIZEDTEXT] );
		UA_LocalizedText_clear( &one );
		EXPECT_EQ( scalar.ToJson().as_string(), "solo" ); //scalar and array agree on the shape.
	}

	//The other arm #3 added.  Note the shape is BrowseName's {ns,name}, matching the scalar case in Variant::ToJson,
	//rather than the vendor's UA-json {Name,Uri}.
	TEST( ValueTests, QualifiedNameArraySerializesPerElement ){
		UA_QualifiedName names[2]{};
		names[0] = UA_QualifiedName{ 2, AllocUAString(string{"First"}) };
		names[1] = UA_QualifiedName{ 3, AllocUAString(string{"Second"}) };
		let v = arrayDataValue( names, 2u, UA_TYPES[UA_TYPES_QUALIFIEDNAME] );
		for( auto& n : names )
			UA_QualifiedName_clear( &n );
		let j = v.ToJson();
		ASSERT_EQ( j.as_array().size(), 2u );
		EXPECT_EQ( j.as_array()[0].at("name").as_string(), "First" );
		EXPECT_EQ( j.as_array()[0].at("ns").to_number<int>(), 2 );
		EXPECT_EQ( j.as_array()[1].at("name").as_string(), "Second" );
	}

	//The #3/#11 composite, and the one input that used to take the gateway down.  A nested variant has no IS() arm, so it
	//goes through the ε fallback encoder; at 4 KB it exceeded #11's 2096-byte cap, and the throw crossed ToJson's Ι
	//boundary - std::terminate, driven purely by what the OPC server publishes.  #11 removed the cap so it now encodes,
	//and #3's catch stays as defence for any other encoder failure.  If the Ι boundary regresses the whole runner aborts
	//rather than one test failing: a noexcept violation is not catchable.
	TEST( ValueTests, LargeNestedVariantEncodesThroughToJson ){
		let big = string( 4096, 'x' );
		let uaBig = ToUV( big );
		UA_Variant inner{};
		UA_Variant_setScalarCopy( &inner, &uaBig, &UA_TYPES[UA_TYPES_STRING] );
		let v = dataValue( &inner, UA_TYPES[UA_TYPES_VARIANT] );
		UA_Variant_clear( &inner );

		let j = v.ToJson();
		ASSERT_FALSE( j.is_null() );
		EXPECT_GT( serialize(j).size(), big.size() ) << "the payload has to survive the encode, not be dropped or capped";
	}

	//#13 (fixed): AsNumber<T> round-tripped through ToJson, which returns {high,low,unsigned} for the 64-bit integers, and
	//Json::AsNumber then threw on an object.  SoakRunner and SubscribeTests both call it, and the Int64/UInt64 legs only
	//WARN, so a healthy server read as lost round trips.  It reads the scalar straight out of value.data now.
	TEST( ValueTests, AsNumberReadsSixtyFourBitIntegers ){
		const UA_Int64 i64{ 42 };
		EXPECT_EQ( dataValue(&i64, UA_TYPES[UA_TYPES_INT64]).AsNumber<_int>(), 42 );

		const UA_UInt64 u64{ 42 };
		EXPECT_EQ( dataValue(&u64, UA_TYPES[UA_TYPES_UINT64]).AsNumber<uint>(), 42u );

		//past 2^53, where a detour through a double would start rounding.
		const UA_Int64 big{ 9'007'199'254'740'993 };
		EXPECT_EQ( dataValue(&big, UA_TYPES[UA_TYPES_INT64]).AsNumber<_int>(), 9'007'199'254'740'993 );
		const UA_UInt64 huge{ 18'446'744'073'709'551'615ull };
		EXPECT_EQ( dataValue(&huge, UA_TYPES[UA_TYPES_UINT64]).AsNumber<uint>(), 18'446'744'073'709'551'615ull );
		const UA_Int64 negative{ -9'007'199'254'740'993 };
		EXPECT_EQ( dataValue(&negative, UA_TYPES[UA_TYPES_INT64]).AsNumber<_int>(), -9'007'199'254'740'993 );
	}

	//Every numeric scalar, since a direct read supports a different set than the json detour did.
	TEST( ValueTests, AsNumberCoversTheNumericScalars ){
		const UA_Boolean b{ true };
		EXPECT_EQ( dataValue(&b, UA_TYPES[UA_TYPES_BOOLEAN]).AsNumber<int>(), 1 );
		const UA_SByte sbyte{ -3 };
		EXPECT_EQ( dataValue(&sbyte, UA_TYPES[UA_TYPES_SBYTE]).AsNumber<int>(), -3 );
		const UA_Byte byte{ 200 };
		EXPECT_EQ( dataValue(&byte, UA_TYPES[UA_TYPES_BYTE]).AsNumber<int>(), 200 );
		const UA_Int16 i16{ -300 };
		EXPECT_EQ( dataValue(&i16, UA_TYPES[UA_TYPES_INT16]).AsNumber<int>(), -300 );
		const UA_UInt32 u32{ 42 };
		EXPECT_EQ( dataValue(&u32, UA_TYPES[UA_TYPES_UINT32]).AsNumber<uint>(), 42u );
		const UA_Float f{ 1.5f };
		EXPECT_DOUBLE_EQ( dataValue(&f, UA_TYPES[UA_TYPES_FLOAT]).AsNumber<double>(), 1.5 );
		const UA_StatusCode sc{ UA_STATUSCODE_GOODCLAMPED };
		EXPECT_EQ( dataValue(&sc, UA_TYPES[UA_TYPES_STATUSCODE]).AsNumber<uint32_t>(), UA_STATUSCODE_GOODCLAMPED );

		let text = ToUV( "abc" );
		EXPECT_THROW( dataValue(&text, UA_TYPES[UA_TYPES_STRING]).AsNumber<int>(), Exception ); //not a number, and says so.
	}

	//The json path rejected a value too wide for T; a direct read must not quietly truncate instead.
	//review3 #13: AsNumber<Integral> on a Float/Double node was `(T)Get<UA_Double>(0)` with no check at all.  [conv.fpint]
	//is undefined for a NaN, an infinity, or a value whose truncation does not fit, and x86-64's cvttsd2si answers
	//INT64_MIN quietly - a failed sensor came back as 9223372036854775808 instead of failing - while clang's
	//-fsanitize=undefined group does not include float-cast-overflow, so the debug build never trapped it either.
	TEST( ValueTests, AsNumberBoundsAFloatSource ){
		let nan = std::numeric_limits<UA_Double>::quiet_NaN();
		let inf = std::numeric_limits<UA_Double>::infinity();
		EXPECT_THROW( dataValue(&nan, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<uint>(), Exception );
		EXPECT_THROW( dataValue(&inf, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<int>(), Exception );
		const UA_Double big{ 1e12 };
		EXPECT_THROW( dataValue(&big, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<int32_t>(), Exception );
		const UA_Double negative{ -1.5 };
		EXPECT_THROW( dataValue(&negative, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<uint>(), Exception );//the soak runner's case.

		//The top of the range is where a `(U)numeric_limits<T>::max()` bound would round up and let one value through.
		const UA_Double past{ 9223372036854775808.0 };//2^63
		EXPECT_THROW( dataValue(&past, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<int64_t>(), Exception );
		const UA_Double top{ 9223372036854774784.0 };//the largest double below 2^63
		EXPECT_EQ( dataValue(&top, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<int64_t>(), 9223372036854774784LL );

		//In range it still truncates rather than demanding exactness - the live callers read a Float sensor and want its
		//integral part, which is where this differs from the json path the old comment claimed parity with.
		const UA_Double fraction{ 42.7 };
		EXPECT_EQ( dataValue(&fraction, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<uint>(), 42u );
		const UA_Float f{ 42.7f };
		EXPECT_EQ( dataValue(&f, UA_TYPES[UA_TYPES_FLOAT]).AsNumber<uint>(), 42u );
		//A floating target is unaffected either way.
		EXPECT_DOUBLE_EQ( dataValue(&fraction, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<double>(), 42.7 );
		EXPECT_TRUE( std::isnan(dataValue(&nan, UA_TYPES[UA_TYPES_DOUBLE]).AsNumber<double>()) );
	}

	TEST( ValueTests, AsNumberRejectsAValueTooWideForTheTarget ){
		const UA_Int64 big{ 1'000'000 };
		EXPECT_THROW( dataValue(&big, UA_TYPES[UA_TYPES_INT64]).AsNumber<int16_t>(), Exception );
		const UA_Int32 negative{ -1 };
		EXPECT_THROW( dataValue(&negative, UA_TYPES[UA_TYPES_INT32]).AsNumber<uint>(), Exception );
		EXPECT_EQ( dataValue(&big, UA_TYPES[UA_TYPES_INT64]).AsNumber<int32_t>(), 1'000'000 ); //fits, so no throw.
	}

	//An empty or status-only value used to report the misleading "Arrays Not implemented."
	TEST( ValueTests, AsNumberOnAnEmptyValueNamesTheStatus ){
		Value bad{ (StatusCode)UA_STATUSCODE_BADNODEIDUNKNOWN };
		EXPECT_THROW( bad.AsNumber<uint>(), Exception );
		try{ bad.AsNumber<uint>(); }
		catch( const Exception& e ){ EXPECT_NE( string{e.what()}.find("80340000"), string::npos ) << e.what(); }

		const UA_Int32 values[]{ 1, 2 };
		EXPECT_THROW( arrayDataValue(values, 2u, UA_TYPES[UA_TYPES_INT32]).AsNumber<int>(), Exception );
	}

	Ω statusValue( StatusCode sc, const UA_Double* d )ι->Value{
		UA_DataValue dv{};
		if( d ){
			UA_Variant_setScalarCopy( &dv.value, d, &UA_TYPES[UA_TYPES_DOUBLE] );
			dv.hasValue = true;
		}
		dv.hasStatus = true;
		dv.status = sc;
		return Value{ move(dv) };
	}

	//#15 (fixed): any non-zero StatusCode replaced the reading, but an Uncertain code (0x4xxxxxxx) carries a usable value
	//per OPC-UA - a sensor reporting UNCERTAINLASTUSABLEVALUE with 42.7 showed up as a blank tag.  Now a non-zero code
	//rides along with the reading - {"v":…,"sc":…} - rather than replacing it; only Bad still does.
	TEST( ValueTests, UncertainQualityKeepsTheValue ){
		const UA_Double d{ 42.7 };
		EXPECT_DOUBLE_EQ( statusValue(UA_STATUSCODE_GOOD, &d).ToJson().to_number<double>(), 42.7 );//plain Good stays a bare scalar.

		//Uncertain codes and an informative Good sub-code all report both halves.
		for( let sc : { UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE, UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE,
				UA_STATUSCODE_UNCERTAIN, UA_STATUSCODE_GOODCLAMPED } ){
			let j = statusValue( sc, &d ).ToJson();
			EXPECT_DOUBLE_EQ( j.at("v").to_number<double>(), 42.7 ) << Ƒ( "0x{:x}", sc );
			EXPECT_EQ( j.at("sc").to_number<uint32_t>(), sc ) << Ƒ( "0x{:x}", sc );
		}

		//...but a Bad one still replaces it: there is no usable reading behind a Bad status.
		for( let sc : { UA_STATUSCODE_BADNODEIDUNKNOWN, UA_STATUSCODE_BADTIMEOUT } ){
			let j = statusValue( sc, &d ).ToJson();
			EXPECT_EQ( j.at("sc").to_number<uint32_t>(), sc ) << Ƒ( "0x{:x}", sc );
			EXPECT_FALSE( j.get_object().contains("v") ) << Ƒ( "0x{:x}", sc );
		}

		//An Uncertain array wraps the same way: the whole array under "v".
		const UA_Int32 values[]{ 1, 2 };
		auto arr = arrayDataValue( values, 2u, UA_TYPES[UA_TYPES_INT32] );
		arr.status = UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE; arr.hasStatus = true;
		let ja = arr.ToJson();
		EXPECT_EQ( ja.at("sc").to_number<uint32_t>(), UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE );
		ASSERT_TRUE( ja.at("v").is_array() );
		EXPECT_EQ( ja.at("v").as_array().size(), 2u );
		EXPECT_EQ( ja.at("v").as_array()[1].to_number<int>(), 2 );
	}

	//The other shape ToJson emits that Set could not read back: a reading carrying a status, {v,sc} - review2 #15.
	TEST( ValueTests, SetUnwrapsTheStatusWrapper ){
		const UA_Double d{ 42.7 };
		let j = statusValue( UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE, &d ).ToJson();
		ASSERT_TRUE( j.as_object().contains("sc") );
		EXPECT_DOUBLE_EQ( Value( j, &UA_TYPES[UA_TYPES_DOUBLE] ).Get<UA_Double>(0), 42.7 );

		//Nested, since an Uncertain 64-bit reading is {v:{high,low,unsigned},sc}.
		const UA_Int64 i{ -42 };
		auto uncertain = dataValue( &i, UA_TYPES[UA_TYPES_INT64] );
		uncertain.status = UA_STATUSCODE_UNCERTAIN; uncertain.hasStatus = true;
		let nested = uncertain.ToJson();
		ASSERT_TRUE( nested.at("v").is_object() );
		EXPECT_EQ( Value( nested, &UA_TYPES[UA_TYPES_INT64] ).Get<UA_Int64>(0), -42 );
	}

	//With nothing to show, the status is still worth reporting - otherwise an Uncertain-but-empty value became a bare null.
	TEST( ValueTests, AnEmptyValueStillReportsItsStatus ){
		EXPECT_EQ( statusValue(UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE, nullptr).ToJson().at("sc").to_number<uint32_t>(),
			UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE );
		EXPECT_TRUE( Value( UA_DataValue{} ).ToJson().is_null() ); //no status, no value: still just null.
	}

	//#20: Get<T> reinterprets value.data, it does not convert, so it has to be handed the raw UA type and the wrapper
	//constructed from that.  Asking Get for a UADateTime read a tick count as a TimePoint instead: a 2001 date came back
	//as 12644473600s - off by exactly the 1601->1970 offset, 369 years - and a 1500ms Duration as 465431188521 seconds.
	TEST( ValueTests, GetReturnsTheRawUaTypeNotAWrapper ){
		const UA_DateTime dt = UA_DateTime_fromUnixTime( 1'000'000'000 );
		let v = dataValue( &dt, UA_TYPES[UA_TYPES_DATETIME] );
		EXPECT_EQ( v.Get<UA_DateTime>(0), dt );
		EXPECT_EQ( UADateTime{v.Get<UA_DateTime>(0)}.ToProto().seconds(), 1'000'000'000 );

		const UA_Duration ms{ 1500 };//a millisecond double, not a time point - there is no wrapper to ask for at all.
		EXPECT_DOUBLE_EQ( dataValue(&ms, UA_TYPES[UA_TYPES_DURATION]).Get<UA_Duration>(0), 1500. );
	}

	//L1: ToJson emits a Duration as a plain millisecond number while Set demanded a {seconds,nanos} object, so writing
	//back a value that was just read threw in as_object().
	TEST( ValueTests, DurationRoundTrip ){
		const UA_Duration ms{ 1500 };
		let read = dataValue( &ms, UA_TYPES[UA_TYPES_DURATION] ).ToJson();
		EXPECT_DOUBLE_EQ( read.to_number<double>(), 1500. );//a bare number, which is what has to come back.
		auto write = dataValue( &ms, UA_TYPES[UA_TYPES_DURATION] );
		ASSERT_NO_THROW( write.Set(read) );
		EXPECT_DOUBLE_EQ( write.ToJson().to_number<double>(), 1500. );
		EXPECT_EQ( write.value.type, &UA_TYPES[UA_TYPES_DURATION] );//still a Duration, not demoted to a plain Double.

		//The object form has to keep working: the socket sends a Duration as a google.protobuf.Duration, so a client can
		//be writing back {seconds,nanos} for the very same node.
		auto fromObject = dataValue( &ms, UA_TYPES[UA_TYPES_DURATION] );
		ASSERT_NO_THROW( fromObject.Set(jvalue{jobject{ {"seconds",2}, {"nanos",500'000'000} }}) );
		EXPECT_DOUBLE_EQ( fromObject.ToJson().to_number<double>(), 2500. );

		//Double takes both spellings too - it always did, and merging the branches must not have cost it either.
		const UA_Double d{ 1500 };
		auto dbl = dataValue( &d, UA_TYPES[UA_TYPES_DOUBLE] );
		ASSERT_NO_THROW( dbl.Set(jvalue{1500.}) );
		EXPECT_DOUBLE_EQ( dbl.ToJson().to_number<double>(), 1500. );
		ASSERT_NO_THROW( dbl.Set(jvalue{jobject{ {"seconds",1}, {"nanos",500'000'000} }}) );
		EXPECT_DOUBLE_EQ( dbl.ToJson().to_number<double>(), 1500. );
	}

	//L2: the old `seconds + nanos` added two chrono durations whose common type is int64 *nanoseconds*, so scaling the
	//seconds up overflowed past ~292 years.  Signed overflow, so UB - a UBSan abort where it is enabled, and a silently
	//wrapped value everywhere else, from a number a client picked.
	TEST( ValueTests, DurationSecondsPastTheNanosecondRange ){
		const UA_Duration ms{ 0 };
		auto v = dataValue( &ms, UA_TYPES[UA_TYPES_DURATION] );
		for( let seconds : { 100'000'000'000LL, -100'000'000'000LL } ){//~3170 years, an order past where it used to wrap.
			ASSERT_NO_THROW( v.Set(jvalue{jobject{ {"seconds", seconds}, {"nanos", 0} }}) ) << seconds;
			EXPECT_DOUBLE_EQ( v.ToJson().to_number<double>(), (double)seconds*1000. ) << seconds;
		}
	}

	//Below the cut (fixed): the typeless Set deduced null/int64/uint64/double/string but not bool, so writing `true` to a
	//node whose DataType had not been resolved failed with "Value has no type."  Fixed as part of #8 rather than on its
	//own: Variant( jvalue, dataType ) now delegates here, so leaving the gap would have regressed a `{value:true}` config
	//that used to work through Variant's own inference.
	TEST( ValueTests, TypelessSetInfersBool ){
		Value v{ UA_DataValue{} };
		ASSERT_NO_THROW( v.Set(jvalue{true}) );
		EXPECT_TRUE( v.ToJson().as_bool() );
		EXPECT_EQ( v.value.type, &UA_TYPES[UA_TYPES_BOOLEAN] );
	}

	//#195, the other direction:  a Proto::Value - what the historian's files and the pass-through hold - through a Variant
	//and back is the message it started as, member for member.
	Ω expectRoundTrip( const Proto::Value& proto )ι->void{
		try{
			let round = ProtoUtils::ToValue( ProtoUtils::ToVariant(proto) );
			EXPECT_EQ( round.SerializeAsString(), proto.SerializeAsString() ) << proto.ShortDebugString() << " came back as " << round.ShortDebugString();
		}
		catch( const std::exception& e ){
			ADD_FAILURE() << proto.ShortDebugString() << " - " << e.what();
		}
	}
	Ω nodeId( uint32 ns, uint32 numeric )ι->Proto::NodeId{
		Proto::NodeId y;
		y.set_namespace_index( ns );
		y.set_numeric( numeric );
		return y;
	}

	TEST( ValueTests, EveryProtoMemberRoundTrips ){
		vector<Proto::Value> values;
		auto add = [&]( auto set ){ set( values.emplace_back() ); };
		add( []( auto& v ){ v.set_boolean( false ); } );//a default is still a member, not an empty Value.
		add( []( auto& v ){ v.set_byte( 200 ); } );
		add( []( auto& v ){ v.set_byte_string( string{"\x00\xff", 2} ); } );
		add( []( auto& v ){ auto& d = *v.mutable_date(); d.set_seconds( -1 ); d.set_nanos( 999'999'900 ); } );//just before 1970, to the 100ns.
		add( []( auto& v ){ v.set_double_value( 2.5 ); } );
		add( []( auto& v ){ auto& d = *v.mutable_duration(); d.set_seconds( 1 ); d.set_nanos( 500'000'000 ); } );
		add( []( auto& v ){ auto& x = *v.mutable_expanded_node(); x.mutable_node()->set_string( "Tag1" ); x.mutable_node()->set_namespace_index( 3 ); x.set_namespace_uri( "urn:plc" ); x.set_server_index( 1 ); } );
		add( []( auto& v ){ v.set_float_value( 1.25f ); } );
		add( []( auto& v ){ v.set_guid( string{"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", 16} ); } );
		add( []( auto& v ){ v.set_int16( -300 ); } );
		add( []( auto& v ){ v.set_int32( -70'000 ); } );
		add( []( auto& v ){ v.set_int64( std::numeric_limits<int64_t>::min() ); } );
		add( []( auto& v ){ auto& n = *v.mutable_node(); n.set_namespace_index( 1 ); n.set_byte_string( string{"\xde\xad", 2} ); } );
		add( []( auto& v ){ auto& n = *v.mutable_node(); n.set_namespace_index( 1 ); n.set_string( string{"a\0b", 3} ); } );//by size, past the NUL.
		add( []( auto& v ){ v.mutable_node()->set_guid( string{"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", 16} ); } );
		add( []( auto& v ){ v.set_sbyte( -5 ); } );
		add( []( auto& v ){ v.set_status_code( UA_STATUSCODE_BADNODEIDUNKNOWN ); } );
		add( []( auto& v ){ v.set_string_value( "tag value" ); } );
		add( []( auto& v ){ v.set_uint16( 60'000 ); } );
		add( []( auto& v ){ v.set_uint32( 4'000'000'000u ); } );
		add( []( auto& v ){ v.set_uint64( std::numeric_limits<uint64_t>::max() ); } );
		add( []( auto& v ){ v.set_xml_element( "<a/>" ); } );
		add( []( auto& v ){ auto& t = *v.mutable_localized_text(); t.set_locale( "en-US" ); t.set_text( "Hello" ); } );
		add( []( auto& v ){ v.mutable_localized_text()->set_text( "text only" ); } );
		add( []( auto& v ){ auto& q = *v.mutable_qualified_name(); q.set_namespace_index( 2 ); q.set_name( "Temperature" ); } );
		add( []( auto& v ){ auto& x = *v.mutable_extension_object(); *x.mutable_type_id() = nodeId( 2, 5001 ); x.set_binary( string{"\x01\x02", 2} ); } );
		add( []( auto& v ){ auto& x = *v.mutable_extension_object(); *x.mutable_type_id() = nodeId( 2, 5002 ); x.set_xml( "<Range/>" ); } );
		add( []( auto& v ){ *v.mutable_extension_object()->mutable_type_id() = nodeId( 2, 5003 ); } );//no body
		add( []( auto& v ){
			auto& a = *v.mutable_array();
			a.set_type( UA_TYPES_INT32+1 );
			for( int i=1; i<=4; ++i )
				a.add_values()->set_int32( i );
			a.add_dimensions( 2 ); a.add_dimensions( 2 );
		} );
		add( []( auto& v ){ v.mutable_array()->set_type( UA_TYPES_STRING+1 ); } );//empty, and still a String array.
		add( []( auto& v ){ auto& a = *v.mutable_array(); a.set_type( UA_TYPES_STRING+1 ); a.set_is_null( true ); } );//length -1, not 0.
		add( []( auto& v ){
			auto& a = *v.mutable_array();
			a.set_type( UA_TYPES_VARIANT+1 );
			a.add_values()->set_int32( 5 );
			a.add_values()->set_string_value( "x" );
			a.add_values()->mutable_array()->set_type( UA_TYPES_DOUBLE+1 );
		} );
		add( []( auto& ){} );//no member at all:  an empty Variant.
		for( let& v : values )
			expectRoundTrip( v );
	}

	//A hand-built message can say what no Variant can hold; it is refused rather than guessed at.
	TEST( ValueTests, AMalformedArrayIsRejected ){
		Proto::Value mixed;
		mixed.mutable_array()->add_values()->set_int32( 1 );
		mixed.mutable_array()->add_values()->set_string_value( "x" );
		EXPECT_THROW( ProtoUtils::ToVariant(mixed), UAException ) << "mixed types need the Variant type";

		Proto::Value wrongType;
		wrongType.mutable_array()->set_type( UA_TYPES_STRING+1 );
		wrongType.mutable_array()->add_values()->set_int32( 1 );
		EXPECT_THROW( ProtoUtils::ToVariant(wrongType), UAException );

		Proto::Value emptyFirst;
		emptyFirst.mutable_array()->set_type( UA_TYPES_INT32+1 );
		emptyFirst.mutable_array()->add_values();
		EXPECT_THROW( ProtoUtils::ToVariant(emptyFirst), UAException ) << "only a Variant array may hold a null";

		Proto::Value emptyLater;
		emptyLater.mutable_array()->set_type( UA_TYPES_INT32+1 );
		emptyLater.mutable_array()->add_values()->set_int32( 1 );
		emptyLater.mutable_array()->add_values();
		EXPECT_THROW( ProtoUtils::ToVariant(emptyLater), UAException );

		Proto::Value nested;
		nested.mutable_array()->set_type( UA_TYPES_INT32+1 );
		nested.mutable_array()->add_values()->set_int32( 1 );
		auto& inner = *nested.mutable_array()->add_values()->mutable_array();
		inner.set_type( UA_TYPES_INT32+1 );
		inner.add_values()->set_int32( 2 );
		try{
			ProtoUtils::ToVariant( nested );
			ADD_FAILURE() << "a nested Int32[] converted.";
		}
		catch( const UAException& e ){
			EXPECT_NE( string{e.what()}.find("nested"), string::npos ) << e.what();
		}

		Proto::Value nullWithValues;
		nullWithValues.mutable_array()->set_type( UA_TYPES_INT32+1 );
		nullWithValues.mutable_array()->set_is_null( true );
		nullWithValues.mutable_array()->add_values()->set_int32( 1 );
		EXPECT_THROW( ProtoUtils::ToVariant(nullWithValues), UAException );

		Proto::Value wrongShape;
		wrongShape.mutable_array()->set_type( UA_TYPES_INT32+1 );
		wrongShape.mutable_array()->add_values()->set_int32( 1 );
		wrongShape.mutable_array()->add_dimensions( 2 );
		EXPECT_THROW( ProtoUtils::ToVariant(wrongShape), UAException );

		Proto::Value notBuiltIn;
		notBuiltIn.mutable_array()->set_type( 26 );
		EXPECT_THROW( ProtoUtils::ToVariant(notBuiltIn), UAException );
	}

	//A Timestamp holds seconds no UA_DateTime can; they saturate as UADateTime's Timestamp ctor does.
	TEST( ValueTests, AnOutOfRangeDateSaturates ){
		Proto::Value late;
		late.mutable_date()->set_seconds( 1'000'000'000'000'000 );
		EXPECT_EQ( *(const UA_DateTime*)ProtoUtils::ToVariant(late).data, (std::numeric_limits<UA_DateTime>::max)() );
		Proto::Value early;
		early.mutable_date()->set_seconds( -1'000'000'000'000'000 );
		EXPECT_EQ( *(const UA_DateTime*)ProtoUtils::ToVariant(early).data, (std::numeric_limits<UA_DateTime>::min)() );
	}

	//What the gateway's tests and soak runner read a pushed value with.
	TEST( ValueTests, FromAProtoValue ){
		Proto::Value proto;
		proto.set_uint32( 42 );
		Value v{ ProtoUtils::ToVariant(proto) };
		EXPECT_TRUE( v.hasValue );
		EXPECT_EQ( v.AsNumber<uint>(), 42u );
	}
}

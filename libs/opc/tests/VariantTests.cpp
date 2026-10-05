//Variant is the persistence path:  ToUAJson() writes one UA-JSON string per element into the db, ToUAValues() reads
//them back, ToArrayDims()/ArrayDimString() carry the shape alongside.  These are the round trips reviews/opc-review2.md
//says would have caught most of the review.
#include <gtest/gtest.h>
#include <jde/db/Value.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Variant.h>
#include <jde/opc/proto/opc.Common.h>

#define let const auto

namespace Jde::Opc::Tests{
	Ω scalarVariant( const void* value, const UA_DataType& type )ι->UA_Variant{
		UA_Variant v{};
		UA_Variant_setScalarCopy( &v, value, &type );
		return v;
	}
	Ω arrayVariant( const void* values, uint count, const UA_DataType& type )ι->UA_Variant{
		UA_Variant v{};
		UA_Variant_setArrayCopy( &v, values, count, &type );
		return v;
	}
	Ω stored( const vector<string>& uaJson )ι->flat_map<uint,string>{
		flat_map<uint,string> y;
		for( uint i=0; i<uaJson.size(); ++i )
			y.emplace( i, uaJson[i] );
		return y;
	}

	TEST( VariantTests, EmptyAndScalar ){
		EXPECT_TRUE( Variant( UA_Variant{} ).IsNull() );
		EXPECT_TRUE( Variant( UA_Variant{} ).ToUAJson().empty() );

		const UA_Int32 i{ 42 };
		Variant scalar{ scalarVariant(&i, UA_TYPES[UA_TYPES_INT32]) };
		EXPECT_FALSE( scalar.IsNull() );
		EXPECT_TRUE( scalar.IsScalar() );
		EXPECT_EQ( serialize(scalar.ToJson(true)), "42" );
	}

	TEST( VariantTests, StatusCodeScalar ){
		Variant v{ (StatusCode)UA_STATUSCODE_BADNODEIDUNKNOWN };
		EXPECT_TRUE( v.IsScalar() );
		EXPECT_EQ( v.type, &UA_TYPES[UA_TYPES_STATUSCODE] );
	}

	TEST( VariantTests, MoveEmptiesTheSource ){
		const UA_Int32 i{ 42 };
		Variant v{ scalarVariant(&i, UA_TYPES[UA_TYPES_INT32]) };
		auto raw = v.Move();
		EXPECT_TRUE( v.IsNull() );
		EXPECT_EQ( raw.type, &UA_TYPES[UA_TYPES_INT32] );
		UA_Variant_clear( &raw ); //Move() made us the owner.
	}

	//A null dataType means "no declared DataType attribute" - infer from the json kind.
	TEST( VariantTests, FromJsonInfersTheType ){
		EXPECT_EQ( Variant( jvalue{true}, nullptr ).type, &UA_TYPES[UA_TYPES_BOOLEAN] );
		EXPECT_EQ( Variant( jvalue{"abc"}, nullptr ).type, &UA_TYPES[UA_TYPES_STRING] );
		EXPECT_EQ( Variant( jvalue{1.5}, nullptr ).type, &UA_TYPES[UA_TYPES_DOUBLE] );
		EXPECT_EQ( Variant( jvalue{5}, nullptr ).type, &UA_TYPES[UA_TYPES_INT64] );
		EXPECT_EQ( Variant( jvalue{5u}, nullptr ).type, &UA_TYPES[UA_TYPES_UINT64] );
		EXPECT_EQ( serialize(Variant( jvalue{"abc"}, nullptr ).ToJson(true)), R"("abc")" );
		EXPECT_THROW( Variant( jvalue{}, nullptr ), Exception ); //nothing to infer from.
	}

	//#8 (fixed): only the literal "double" used to be honoured; every other declared dataType fell through to json
	//inference, so {dataType:"int", value:5} yielded an Int64 variant against an Int32 DataType attribute and
	//UA_Server_addVariableNode rejected it with BadTypeMismatch.  The declared type now decides, and the value is
	//decoded toward it rather than merely labelled with it.
	TEST( VariantTests, DeclaredDataTypeWins ){
		EXPECT_EQ( Variant( jvalue{5}, &UA_TYPES[UA_TYPES_INT32] ).type, &UA_TYPES[UA_TYPES_INT32] );
		EXPECT_EQ( Variant( jvalue{5}, &UA_TYPES[UA_TYPES_UINT32] ).type, &UA_TYPES[UA_TYPES_UINT32] );
		EXPECT_EQ( Variant( jvalue{5}, &UA_TYPES[UA_TYPES_BYTE] ).type, &UA_TYPES[UA_TYPES_BYTE] );
		EXPECT_EQ( Variant( jvalue{5}, &UA_TYPES[UA_TYPES_INT16] ).type, &UA_TYPES[UA_TYPES_INT16] );
		EXPECT_EQ( Variant( jvalue{5}, &UA_TYPES[UA_TYPES_FLOAT] ).type, &UA_TYPES[UA_TYPES_FLOAT] );
		EXPECT_EQ( Variant( jvalue{5}, &UA_TYPES[UA_TYPES_DOUBLE] ).type, &UA_TYPES[UA_TYPES_DOUBLE] );

		//the value has to arrive as that type, not just be tagged with it.
		Variant i32{ jvalue{5}, &UA_TYPES[UA_TYPES_INT32] };
		ASSERT_TRUE( i32.IsScalar() );
		EXPECT_EQ( *(const UA_Int32*)i32.data, 5 );
		EXPECT_EQ( serialize(i32.ToJson(true)), "5" );

		Variant f{ jvalue{1.5}, &UA_TYPES[UA_TYPES_FLOAT] };
		EXPECT_FLOAT_EQ( *(const UA_Float*)f.data, 1.5f );

		//and a value the declared type cannot hold is rejected rather than silently retyped.
		EXPECT_THROW( Variant( jvalue{"abc"}, &UA_TYPES[UA_TYPES_INT32] ), Exception );
	}

	TEST( VariantTests, ScalarToJsonUsesTheOpcShapeForNodeIds ){
		let n = NodeId{ 2, 5002 };
		Variant v{ scalarVariant(static_cast<const UA_NodeId*>(&n), UA_TYPES[UA_TYPES_NODEID]) };
		let j = v.ToJson( true );
		EXPECT_EQ( j.at("ns").to_number<int>(), 2 );
		EXPECT_EQ( j.at("i").to_number<int>(), 5002 );
	}

	//review2 #3's note, closed as O4: the NODEID/QUALIFIEDNAME arms lived in ToJson's *scalar* branch only, so one type
	//gave two shapes depending on its value rank - {ns,i} on its own, the vendor's UA-json spelling inside an array.
	//The arms moved into ElementToJson, which both ranks go through.  The UtcTime case is the other half: dispatch was on
	//the descriptor address, so an alias took the vendor spelling here while Value::ToJson gave it {seconds,nanos}.
	TEST( VariantTests, ArrayElementsUseTheSameShapeAsAScalar ){
		const UA_NodeId ids[]{ {2, UA_NODEIDTYPE_NUMERIC, {5002}}, {3, UA_NODEIDTYPE_NUMERIC, {5003}} };
		let arrayJson = Variant{ arrayVariant(ids, 2u, UA_TYPES[UA_TYPES_NODEID]) }.ToJson( true );
		ASSERT_TRUE( arrayJson.is_array() );
		EXPECT_EQ( arrayJson.as_array()[1].at("ns").to_number<int>(), 3 );
		EXPECT_EQ( arrayJson.as_array()[1].at("i").to_number<int>(), 5003 );

		let scalarJson = Variant{ scalarVariant(&ids[1], UA_TYPES[UA_TYPES_NODEID]) }.ToJson( true );
		EXPECT_EQ( serialize(arrayJson.as_array()[1]), serialize(scalarJson) ) << "one type, one shape, whatever the value rank";

		//UtcTime is a DateTime by kind, so it takes the DateTime arm rather than the vendor's spelling.
		const UA_DateTime utc{ UA_DateTime_fromUnixTime(1700000000) };
		let utcJson = Variant{ scalarVariant(&utc, UA_TYPES[UA_TYPES_UTCTIME]) }.ToJson( true );
		EXPECT_EQ( utcJson.at("seconds").to_number<int64_t>(), 1700000000 );
	}

	TEST( VariantTests, ArrayToJsonIsPerElement ){
		const UA_Int32 values[]{ 1, 2, 3 };
		Variant v{ arrayVariant(values, 3u, UA_TYPES[UA_TYPES_INT32]) };
		EXPECT_FALSE( v.IsScalar() );
		EXPECT_EQ( serialize(v.ToJson(true)), "[1,2,3]" );
	}

	TEST( VariantTests, ArrayDims ){
		let dims = Variant::ToArrayDims( "2,3" );
		ASSERT_EQ( get<1>(dims), 2u );
		EXPECT_EQ( get<0>(dims)[0], 2u );
		EXPECT_EQ( get<0>(dims)[1], 3u );

		auto data = UA_Array_new( 6, &UA_TYPES[UA_TYPES_INT32] );
		ASSERT_NE( data, nullptr );
		Variant v{ 0, {6u, data}, dims, UA_TYPES[UA_TYPES_INT32] }; //takes ownership of both allocations.
		EXPECT_EQ( v.ArrayDimString(), "2,3" );

		let none = Variant::ToArrayDims( "" );
		EXPECT_EQ( get<0>(none), nullptr );
		EXPECT_EQ( get<1>(none), 0u );
		EXPECT_TRUE( Variant( UA_Variant{} ).ArrayDimString().empty() );
	}

	//The persistence round trip.  ToUAJson emits one raw UA-JSON token per element and ToUAValues decodes them back;
	//the strings have to reach the db unmodified.
	TEST( VariantTests, UaJsonArrayRoundTrip ){
		const UA_Int32 values[]{ 1, 2, 3 };
		Variant original{ arrayVariant(values, 3u, UA_TYPES[UA_TYPES_INT32]) };
		let json = original.ToUAJson();
		ASSERT_EQ( json.size(), 3u );
		EXPECT_EQ( json[0], "1" );
		EXPECT_EQ( json[2], "3" );

		let [count, data] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], stored(json), true );
		ASSERT_NE( data, nullptr );
		ASSERT_EQ( count, 3u );
		Variant round{ 0, {count, data}, Variant::ToArrayDims("3"), UA_TYPES[UA_TYPES_INT32] };
		EXPECT_EQ( serialize(round.ToJson(true)), "[1,2,3]" );
		EXPECT_EQ( round.ArrayDimString(), "3" );
	}

	TEST( VariantTests, UaJsonScalarRoundTrip ){
		let text = ToUV( "tag value" );
		Variant original{ scalarVariant(&text, UA_TYPES[UA_TYPES_STRING]) };
		let json = original.ToUAJson();
		ASSERT_EQ( json.size(), 1u );
		EXPECT_EQ( json[0], R"("tag value")" );

		let [count, data] = Variant::ToUAValues( UA_TYPES[UA_TYPES_STRING], stored(json), false );
		ASSERT_NE( data, nullptr );
		EXPECT_EQ( count, 0u ); //a single value decodes as a scalar - arrayLength 0, data non-null.
		Variant round{ 0, {count, data}, Variant::ToArrayDims(""), UA_TYPES[UA_TYPES_STRING] };
		EXPECT_TRUE( round.IsScalar() );
		EXPECT_EQ( serialize(round.ToJson(true)), R"("tag value")" );
	}

	TEST( VariantTests, ToUAValuesNeedsRawUaJson ){
		flat_map<uint,string> raw;
		raw.emplace( 0u, "5" );
		let [rawCount, rawData] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], move(raw), false );
		ASSERT_NE( rawData, nullptr );
		EXPECT_EQ( rawCount, 0u );
		EXPECT_EQ( *(const UA_Int32*)rawData, 5 );
		UA_delete( rawData, &UA_TYPES[UA_TYPES_INT32] );

		flat_map<uint,string> quoted;
		quoted.emplace( 0u, serialize(jvalue{"5"}) ); //exactly what serialize(array[i]) writes today.
		let [quotedCount, quotedData] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], move(quoted), false );
		EXPECT_EQ( quotedCount, 0u );
		EXPECT_EQ( quotedData, nullptr ) << R"(an Int32 column holding "5" must not decode)";
	}


	//#10 (fixed): a one-element array collapsed to a scalar ("size==1 would be an array") while the caller still applied
	//the persisted arrayDimensions, producing scalar data with arrayDimensionsSize 1 - open62541 answers BadTypeMismatch,
	//so one-element arrays did not survive a restart.  The stored dims, not the element count, decide.
	TEST( VariantTests, OneElementArrayStaysAnArray ){
		const UA_Int32 one[]{ 5 };
		Variant original{ arrayVariant(one, 1u, UA_TYPES[UA_TYPES_INT32]) };
		ASSERT_FALSE( original.IsScalar() );
		let json = original.ToUAJson();
		ASSERT_EQ( json.size(), 1u ); //indistinguishable from a scalar by count alone - hence the isArray argument.

		let [count, data] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], stored(json), true );
		ASSERT_NE( data, nullptr );
		ASSERT_EQ( count, 1u );
		Variant round{ 0, {count, data}, Variant::ToArrayDims("1"), UA_TYPES[UA_TYPES_INT32] };
		EXPECT_FALSE( round.IsScalar() ) << "scalar data carrying arrayDimensions is what open62541 rejects";
		EXPECT_EQ( serialize(round.ToJson(true)), "[5]" );
		EXPECT_EQ( round.ArrayDimString(), "1" );
	}

	//...and the same single stored element still reloads as a scalar when no dims were persisted.
	TEST( VariantTests, OneElementScalarStaysAScalar ){
		flat_map<uint,string> one;
		one.emplace( 0u, "5" );
		let [count, data] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], move(one), false );
		ASSERT_NE( data, nullptr );
		EXPECT_EQ( count, 0u );
		Variant round{ 0, {count, data}, Variant::ToArrayDims(""), UA_TYPES[UA_TYPES_INT32] };
		EXPECT_TRUE( round.IsScalar() );
		EXPECT_EQ( serialize(round.ToJson(true)), "5" );
	}

	//review3 #3: ArrayDimString() returns "" for a scalar and VariantInsertAwait bound that string, which sqlite, mysql
	//and odbc all store as a *non-null* empty text - so VariantAwait's `isArray`, derived from the column, was true for
	//every row and every persisted scalar reloaded as a one-element array.  UA_Server_writeValue then answers
	//BadTypeMismatch for array data on a scalar-ranked member, i.e. every object instantiated after a restart failed.
	//ArrayDimValue() owns the shape instead.  This walks the caller's contract in VariantAwait's order: bind the value,
	//derive isArray from what came back, hand that to ToUAValues.
	TEST( VariantTests, AScalarPersistsNullDimensions ){
		const UA_Int32 i{ 5 };
		Variant scalar{ scalarVariant(&i, UA_TYPES[UA_TYPES_INT32]) };
		ASSERT_TRUE( scalar.IsScalar() );
		EXPECT_TRUE( scalar.ArrayDimString().empty() );
		EXPECT_FALSE( DB::Value{string{}}.is_null() ) << "the old binding: an empty dims string is a value, not NULL";
		let dims = scalar.ArrayDimValue();
		ASSERT_TRUE( dims.is_null() ) << "a non-null column is what made the reload call this an array";

		let [count, data] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], stored(scalar.ToUAJson()), !dims.is_null() );
		ASSERT_NE( data, nullptr );
		EXPECT_EQ( count, 0u );
		Variant round{ 0, {count, data}, Variant::ToArrayDims(""), UA_TYPES[UA_TYPES_INT32] };
		EXPECT_TRUE( round.IsScalar() );
		EXPECT_EQ( serialize(round.ToJson(true)), "5" );
	}

	//The other half, and review2 #10's residual: an array that declares no arrayDimensions - what UA_Variant_setArrayCopy
	//leaves - would reload as a scalar if the column were NULL for it too, since one element cannot be told from a scalar
	//by its count.  It persists its length instead, so the same walk keeps it an array.
	TEST( VariantTests, AnArrayWithNoDimensionsPersistsItsLength ){
		const UA_Int32 one[]{ 7 };
		Variant array{ arrayVariant(one, 1, UA_TYPES[UA_TYPES_INT32]) };
		ASSERT_FALSE( array.IsScalar() );
		EXPECT_TRUE( array.ArrayDimString().empty() ) << "setArrayCopy sets arrayLength, not arrayDimensions";
		let dims = array.ArrayDimValue();
		ASSERT_FALSE( dims.is_null() );
		EXPECT_EQ( dims.get_string(), "1" );

		let [count, data] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], stored(array.ToUAJson()), !dims.is_null() );
		ASSERT_NE( data, nullptr );
		EXPECT_EQ( count, 1u );
		Variant round{ 0, {count, data}, Variant::ToArrayDims(dims.get_string()), UA_TYPES[UA_TYPES_INT32] };
		EXPECT_FALSE( round.IsScalar() );
		EXPECT_EQ( serialize(round.ToJson(true)), "[7]" );
		EXPECT_EQ( round.ArrayDimValue().get_string(), "1" ) << "write->read->write has to be stable";
	}

	//review3 #12: ToUAValues documents "the variant loads as null" and returns {0,nullptr} for a row it cannot decode,
	//but the receiving ctor stamped `type` and the caller's arrayDimensions onto it anyway.  The result was a *typed*
	//variant with data==NULL - UA_Variant_isEmpty looks at `type` alone, so IsNull() was false and ToJson() gave [] -
	//and with stored dims the binary encoder answers BadEncodingError, which closes the channel on every Read that
	//touches the node.  Logs one expected ERR, the same one ToUAValuesNeedsRawUaJson above provokes.
	TEST( VariantTests, ADecodeFailureLoadsAsAGenuinelyNullVariant ){
		flat_map<uint,string> quoted;
		quoted.emplace( 0u, serialize(jvalue{"5"}) );//json-quoted: the pre-review2 column shape ToUAValues' comment names.
		let [count, data] = Variant::ToUAValues( UA_TYPES[UA_TYPES_INT32], move(quoted), false );
		ASSERT_EQ( data, nullptr );

		Variant v{ 7, {count, data}, Variant::ToArrayDims("2,3"), UA_TYPES[UA_TYPES_INT32] };//dims the ctor now has to free.
		EXPECT_TRUE( v.IsNull() );
		EXPECT_EQ( v.type, nullptr );
		EXPECT_EQ( v.arrayLength, 0u );
		EXPECT_EQ( v.arrayDimensionsSize, 0u );
		EXPECT_EQ( v.arrayDimensions, nullptr );
		EXPECT_TRUE( v.ToUAJson().empty() );
		EXPECT_EQ( v.VariantPK, 7u );//the pk still identifies the row that could not be read.
	}

	//#11 (fixed): open62541 uses a non-empty outBuf as a hard limit, so the fixed 2096-byte buffer in uaJsonString capped
	//every value - a large but perfectly valid string/ByteString/ExtensionObject threw BadEncodingLimitsExceeded instead
	//of encoding.  uaJsonString now passes an empty UA_String and lets the encoder size it, which also drops a 2 KB
	//malloc per element.
	TEST( VariantTests, LargeValueEncodes ){
		let text = string( 4096, 'x' );
		let ua = ToUV( text );
		Variant v{ scalarVariant(&ua, UA_TYPES[UA_TYPES_STRING]) };
		vector<string> json;
		ASSERT_NO_THROW( json = v.ToUAJson() );
		ASSERT_EQ( json.size(), 1u );
		EXPECT_EQ( json[0].size(), text.size()+2 ); //the value plus its two quotes.
	}

	//#195, the historian's round trip:  Variant -> Proto::Value -> protobuf's own wire -> Variant.  "The same" is the UA
	//binary encoding, the one form a Variant's type survives in:  an alias leaves as its built-in type and a structure as
	//an encoded ExtensionObject, and both encode exactly as the original did.
	Ω binary( const UA_Variant& v )ι->string{
		UAString y;
		EXPECT_EQ( UA_encodeBinary(&v, &UA_TYPES[UA_TYPES_VARIANT], &y, nullptr), UA_STATUSCODE_GOOD );
		return y.ToString();
	}
	Ω roundTrip( const UA_Variant& v )ε->Variant{
		Proto::Value wire;
		EXPECT_TRUE( wire.ParseFromString(ProtoUtils::ToValue(v).SerializeAsString()) );
		return ProtoUtils::ToVariant( wire );
	}
	Ω expectRoundTrip( const UA_Variant& v, sv what )ι->void{
		try{
			EXPECT_EQ( binary(roundTrip(v)), binary(v) ) << what;
		}
		catch( const std::exception& e ){
			ADD_FAILURE() << what << " - " << e.what();
		}
	}
	//As a scalar, and as a two-element array of it.
	Ω expectRoundTrip( const void* value, const UA_DataType& type )ι->void{
		Variant scalar{ scalarVariant(value, type) };
		expectRoundTrip( scalar, Ƒ("scalar {}", type.typeName) );
		auto data = UA_Array_new( 2, &type );
		for( uint i=0; i<2; ++i )
			UA_copy( value, (UA_Byte*)data+i*type.memSize, &type );
		UA_Variant raw{};
		UA_Variant_setArray( &raw, data, 2, &type );
		Variant array{ move(raw) };
		expectRoundTrip( array, Ƒ("array {}", type.typeName) );
	}

	TEST( VariantTests, EveryBuiltInTypeRoundTrips ){
		const UA_Boolean boolean{ true };
		expectRoundTrip( &boolean, UA_TYPES[UA_TYPES_BOOLEAN] );
		const UA_SByte sbyte{ -5 };
		expectRoundTrip( &sbyte, UA_TYPES[UA_TYPES_SBYTE] );
		const UA_Byte byte{ 200 };
		expectRoundTrip( &byte, UA_TYPES[UA_TYPES_BYTE] );
		const UA_Int16 int16{ -300 };
		expectRoundTrip( &int16, UA_TYPES[UA_TYPES_INT16] );
		const UA_UInt16 uint16{ 60'000 };
		expectRoundTrip( &uint16, UA_TYPES[UA_TYPES_UINT16] );
		const UA_Int32 int32{ -70'000 };
		expectRoundTrip( &int32, UA_TYPES[UA_TYPES_INT32] );
		const UA_UInt32 uint32{ 4'000'000'000u };
		expectRoundTrip( &uint32, UA_TYPES[UA_TYPES_UINT32] );
		const UA_Int64 int64{ std::numeric_limits<UA_Int64>::min() };
		expectRoundTrip( &int64, UA_TYPES[UA_TYPES_INT64] );
		const UA_UInt64 uint64{ std::numeric_limits<UA_UInt64>::max() };
		expectRoundTrip( &uint64, UA_TYPES[UA_TYPES_UINT64] );
		const UA_Float float_{ -0.f };//the sign bit has to survive, and a NaN's payload below.
		expectRoundTrip( &float_, UA_TYPES[UA_TYPES_FLOAT] );
		const UA_Double double_{ std::numeric_limits<double>::quiet_NaN() };
		expectRoundTrip( &double_, UA_TYPES[UA_TYPES_DOUBLE] );
		const UA_String string_ = ToUV( sv{"a\0b", 3} );
		expectRoundTrip( &string_, UA_TYPES[UA_TYPES_STRING] );
		const UA_DateTime dateTime{ UA_DateTime_fromUnixTime(1'700'000'000)+1'234'567 };//down to the 100ns digit.
		expectRoundTrip( &dateTime, UA_TYPES[UA_TYPES_DATETIME] );
		const UA_Guid guid{ 0x01020304, 0x0506, 0x0708, {9, 10, 11, 12, 13, 14, 15, 16} };
		expectRoundTrip( &guid, UA_TYPES[UA_TYPES_GUID] );
		const UA_ByteString byteString = ToUV( sv{"\x00\xff\x80", 3} );
		expectRoundTrip( &byteString, UA_TYPES[UA_TYPES_BYTESTRING] );
		const UA_XmlElement xml = ToUV( R"(<a b="c"/>)" );
		expectRoundTrip( &xml, UA_TYPES[UA_TYPES_XMLELEMENT] );
		const UA_NodeId nodeId{ 2, UA_NODEIDTYPE_NUMERIC, {5002} };
		expectRoundTrip( &nodeId, UA_TYPES[UA_TYPES_NODEID] );
		const UA_ExpandedNodeId exNodeId{ {3, UA_NODEIDTYPE_STRING, {.string=ToUV("Tag1")}}, ToUV("urn:plc"), 1 };
		expectRoundTrip( &exNodeId, UA_TYPES[UA_TYPES_EXPANDEDNODEID] );
		const UA_StatusCode statusCode{ UA_STATUSCODE_BADNODEIDUNKNOWN };
		expectRoundTrip( &statusCode, UA_TYPES[UA_TYPES_STATUSCODE] );
		const UA_QualifiedName qualifiedName{ 2, ToUV("Temperature") };
		expectRoundTrip( &qualifiedName, UA_TYPES[UA_TYPES_QUALIFIEDNAME] );
		const UA_LocalizedText localizedText{ ToUV("en-US"), ToUV("Hello") };
		expectRoundTrip( &localizedText, UA_TYPES[UA_TYPES_LOCALIZEDTEXT] );
		const UA_ByteString body = ToUV( sv{"\x01\x02\x03", 3} );
		UA_ExtensionObject extensionObject{ UA_EXTENSIONOBJECT_ENCODED_BYTESTRING, {} };
		extensionObject.content.encoded = { UA_NodeId{2, UA_NODEIDTYPE_NUMERIC, {5001}}, body };
		expectRoundTrip( &extensionObject, UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
	}

	//The other identifier kinds, and the members whose absence is part of the encoding.
	TEST( VariantTests, IdentifiersAndNullMembersRoundTrip ){
		const UA_NodeId ids[]{
			{1, UA_NODEIDTYPE_STRING, {.string=ToUV("a/b")}},
			{1, UA_NODEIDTYPE_GUID, {.guid={0xAABBCCDD, 0x1122, 0x3344, {1, 2, 3, 4, 5, 6, 7, 8}}}},
			{1, UA_NODEIDTYPE_BYTESTRING, {.byteString=ToUV(sv{"\xde\xad", 2})}}
		};
		for( let& id : ids )
			expectRoundTrip( &id, UA_TYPES[UA_TYPES_NODEID] );
		const UA_ExpandedNodeId local{ {0, UA_NODEIDTYPE_NUMERIC, {85}}, UA_STRING_NULL, 0 };
		expectRoundTrip( &local, UA_TYPES[UA_TYPES_EXPANDEDNODEID] );

		//LocalizedText's encoding mask says which members are there, and a missing locale is the common case.
		for( let& lt : {UA_LocalizedText{UA_STRING_NULL, ToUV("text only")}, UA_LocalizedText{ToUV(""), ToUV("")}, UA_LocalizedText{UA_STRING_NULL, UA_STRING_NULL}} )
			expectRoundTrip( &lt, UA_TYPES[UA_TYPES_LOCALIZEDTEXT] );

		UA_ExtensionObject noBody{};
		noBody.content.encoded.typeId = UA_NodeId{ 2, UA_NODEIDTYPE_NUMERIC, {5001} };
		expectRoundTrip( &noBody, UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
		UA_ExtensionObject xmlBody{ UA_EXTENSIONOBJECT_ENCODED_XML, {} };
		xmlBody.content.encoded = { UA_NodeId{2, UA_NODEIDTYPE_NUMERIC, {5002}}, ToUV("<Range/>") };
		expectRoundTrip( &xmlBody, UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
	}

	//UA_DateTime's whole range, not just the part a TimePoint holds.
	TEST( VariantTests, DateTimeExtremesRoundTrip ){
		using Limits = std::numeric_limits<UA_DateTime>;
		for( let dt : {UA_DateTime{0}, UA_DateTime{UA_DATETIME_UNIX_EPOCH-1}, (Limits::min)(), (Limits::max)()} ){
			Variant v{ scalarVariant(&dt, UA_TYPES[UA_TYPES_DATETIME]) };
			let round = roundTrip( v );
			ASSERT_TRUE( round.IsScalar() );
			EXPECT_EQ( *(const UA_DateTime*)round.data, dt );
		}
	}

	//What is not a built-in type goes as the built-in type it encodes as:  an alias as itself, an enum as an Int32 and a
	//structure - bare, or decoded inside an ExtensionObject - as an ExtensionObject holding its binary encoding.
	TEST( VariantTests, AliasesEnumsAndStructuresRoundTrip ){
		const UA_DateTime utc{ UA_DateTime_fromUnixTime(1'700'000'000) };
		expectRoundTrip( &utc, UA_TYPES[UA_TYPES_UTCTIME] );
		const UA_Duration duration{ 1500.5 };
		expectRoundTrip( &duration, UA_TYPES[UA_TYPES_DURATION] );
		const UA_UInt32 integerId{ 7 };
		expectRoundTrip( &integerId, UA_TYPES[UA_TYPES_INTEGERID] );
		const UA_String localeId = ToUV( "de-DE" );
		expectRoundTrip( &localeId, UA_TYPES[UA_TYPES_LOCALEID] );
		const UA_ServerState state{ UA_SERVERSTATE_SHUTDOWN };
		expectRoundTrip( &state, UA_TYPES[UA_TYPES_SERVERSTATE] );

		const UA_Range range{ -1.5, 99.5 };
		expectRoundTrip( &range, UA_TYPES[UA_TYPES_RANGE] );
		UA_EUInformation eu{ ToUV("http://www.opcfoundation.org/UA/units/un/cefact"), 4408652, {ToUV("en"), ToUV("°C")}, {UA_STRING_NULL, ToUV("degree Celsius")} };
		expectRoundTrip( &eu, UA_TYPES[UA_TYPES_EUINFORMATION] );
		UA_ExtensionObject decoded;
		UA_ExtensionObject_setValueNoDelete( &decoded, &eu, &UA_TYPES[UA_TYPES_EUINFORMATION] );
		expectRoundTrip( &decoded, UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );

		const UA_Range ranges[]{ {0, 1}, {2, 3} };
		Variant v{ arrayVariant(ranges, 2u, UA_TYPES[UA_TYPES_RANGE]) };
		expectRoundTrip( v, "Range[]" );
		let proto = ProtoUtils::ToValue( v );
		ASSERT_TRUE( proto.has_array() );
		EXPECT_EQ( proto.array().type(), UA_TYPES_EXTENSIONOBJECT+1 );
		EXPECT_EQ( ProtoUtils::ToVariant(proto).type, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] ) << "encoded, not decoded - nothing here needs the type";
	}

	//The shapes an array can take:  a matrix, an empty one of a type, a Variant array mixing types and nesting an array.
	TEST( VariantTests, ArrayShapesRoundTrip ){
		const UA_Int32 values[]{ 1, 2, 3, 4, 5, 6 };
		UA_Variant matrix{};
		UA_Variant_setArrayCopy( &matrix, values, 6, &UA_TYPES[UA_TYPES_INT32] );
		matrix.arrayDimensions = (UA_UInt32*)UA_Array_new( 2, &UA_TYPES[UA_TYPES_UINT32] );
		matrix.arrayDimensions[0] = 2; matrix.arrayDimensions[1] = 3;
		matrix.arrayDimensionsSize = 2;
		Variant m{ move(matrix) };
		expectRoundTrip( m, "2x3 Int32" );
		EXPECT_EQ( roundTrip(m).ArrayDimString(), "2,3" );

		for( let type : {UA_TYPES_STRING, UA_TYPES_DOUBLE, UA_TYPES_VARIANT} ){
			UA_Variant empty{};
			UA_Variant_setArray( &empty, UA_Array_new(0, &UA_TYPES[type]), 0, &UA_TYPES[type] );
			Variant e{ move(empty) };
			expectRoundTrip( e, Ƒ("empty {}", UA_TYPES[type].typeName) );
			UA_Variant undefined{};
			undefined.type = &UA_TYPES[type];//data NULL:  length -1, which Array_decodeBinary leaves for a server's -1.
			Variant u{ move(undefined) };
			expectRoundTrip( u, Ƒ("null {}", UA_TYPES[type].typeName) );
			EXPECT_NE( binary(u), binary(e) ) << "-1 and 0 have to differ on the wire, or this proves nothing";
		}

		const UA_Int32 five{ 5 };
		let text = ToUV( "x" );
		const UA_Int32 inner[]{ 7, 8 };
		auto mixed = (UA_Variant*)UA_Array_new( 4, &UA_TYPES[UA_TYPES_VARIANT] );
		UA_Variant_setScalarCopy( &mixed[0], &five, &UA_TYPES[UA_TYPES_INT32] );
		UA_Variant_setScalarCopy( &mixed[1], &text, &UA_TYPES[UA_TYPES_STRING] );
		UA_Variant_setArrayCopy( &mixed[2], inner, 2, &UA_TYPES[UA_TYPES_INT32] );
		//mixed[3] stays empty - a Variant array may hold a null.
		UA_Variant raw{};
		UA_Variant_setArray( &raw, mixed, 4, &UA_TYPES[UA_TYPES_VARIANT] );
		Variant v{ move(raw) };
		expectRoundTrip( v, "Variant[]" );

		const UA_Int32 one[]{ 9 };
		Variant single{ arrayVariant(one, 1u, UA_TYPES[UA_TYPES_INT32]) };
		expectRoundTrip( single, "one-element Int32[]" );
		EXPECT_FALSE( roundTrip(single).IsScalar() );

		Variant null{ UA_Variant{} };
		EXPECT_EQ( ProtoUtils::ToValue(null).of_case(), Proto::Value::OF_NOT_SET );
		EXPECT_TRUE( roundTrip(null).IsNull() );
	}

	//The two the spec leaves out of the historian; the gateway sends their status code instead.
	TEST( VariantTests, DataValueAndDiagnosticInfoAreNotSupported ){
		const UA_DataValue dataValue{};
		const UA_DiagnosticInfo diagnosticInfo{};
		const std::pair<const void*, int> cases[]{ {&dataValue, UA_TYPES_DATAVALUE}, {&diagnosticInfo, UA_TYPES_DIAGNOSTICINFO} };
		for( let& [p, type] : cases ){
			Variant v{ scalarVariant(p, UA_TYPES[type]) };
			EXPECT_FALSE( ProtoUtils::Supported(v) ) << UA_TYPES[type].typeName;
			try{
				ProtoUtils::ToValue( v );
				ADD_FAILURE() << UA_TYPES[type].typeName << " converted.";
			}
			catch( const UAException& e ){
				EXPECT_EQ( e.Code(), UA_STATUSCODE_BADNOTSUPPORTED ) << UA_TYPES[type].typeName;
			}
		}
		const double reading{ 1.5 };
		UA_Variant elements[2]{ scalarVariant(&reading, UA_TYPES[UA_TYPES_DOUBLE]), scalarVariant(&reading, UA_TYPES[UA_TYPES_DOUBLE]) };
		Variant supported{ arrayVariant(elements, 2, UA_TYPES[UA_TYPES_VARIANT]) };
		EXPECT_TRUE( ProtoUtils::Supported(supported) );
		UA_Variant_clear( &elements[1] );
		elements[1] = scalarVariant( &diagnosticInfo, UA_TYPES[UA_TYPES_DIAGNOSTICINFO] );
		Variant holding{ arrayVariant(elements, 2, UA_TYPES[UA_TYPES_VARIANT]) };//a Variant array holding one.
		EXPECT_FALSE( ProtoUtils::Supported(holding) );
		for( auto& e : elements )
			UA_Variant_clear( &e );
	}

	//Utf8 is false for just the values whose ToValue protobuf won't parse back:  text in its `string` fields, not `bytes`.
	TEST( VariantTests, Utf8 ){
		const UA_String latin1{ 4, (UA_Byte*)"25\xB0" "C" }, utf8{ 5, (UA_Byte*)"25\xC2\xB0" "C" };
		let expect = []( UA_Variant&& uaVariant, bool valid, sv what ){
			const Variant v{ move(uaVariant) };
			EXPECT_EQ( ProtoUtils::Utf8(v), valid ) << what;
			Proto::Value parsed;
			EXPECT_EQ( parsed.ParseFromString(ProtoUtils::ToValue(v).SerializeAsString()), valid ) << what;
		};
		expect( scalarVariant(&utf8, UA_TYPES[UA_TYPES_STRING]), true, "String" );
		expect( scalarVariant(&latin1, UA_TYPES[UA_TYPES_STRING]), false, "Latin-1 String" );
		expect( scalarVariant(&latin1, UA_TYPES[UA_TYPES_XMLELEMENT]), false, "XmlElement" );
		expect( scalarVariant(&latin1, UA_TYPES[UA_TYPES_BYTESTRING]), true, "ByteString" );
		const UA_String strings[]{ utf8, latin1 };
		expect( arrayVariant(strings, 2, UA_TYPES[UA_TYPES_STRING]), false, "String[]" );
		const UA_LocalizedText text{ utf8, latin1 }, locale{ latin1, utf8 };
		expect( scalarVariant(&text, UA_TYPES[UA_TYPES_LOCALIZEDTEXT]), false, "LocalizedText text" );
		expect( scalarVariant(&locale, UA_TYPES[UA_TYPES_LOCALIZEDTEXT]), false, "LocalizedText locale" );
		const UA_QualifiedName name{ 1, latin1 };
		expect( scalarVariant(&name, UA_TYPES[UA_TYPES_QUALIFIEDNAME]), false, "QualifiedName" );

		UA_NodeId id{ .namespaceIndex=1, .identifierType=UA_NODEIDTYPE_STRING };
		id.identifier.string = latin1;
		expect( scalarVariant(&id, UA_TYPES[UA_TYPES_NODEID]), false, "string NodeId" );
		const UA_ExpandedNodeId inExpanded{ id, {}, 0 }, uri{ UA_NODEID_NUMERIC(0, 85), latin1, 0 };
		EXPECT_FALSE( ProtoUtils::Utf8(inExpanded) );
		expect( scalarVariant(&uri, UA_TYPES[UA_TYPES_EXPANDEDNODEID]), false, "namespace URI" );
		id.identifierType = UA_NODEIDTYPE_BYTESTRING;
		expect( scalarVariant(&id, UA_TYPES[UA_TYPES_NODEID]), true, "ByteString NodeId" );

		UA_ExtensionObject eo{ .encoding=UA_EXTENSIONOBJECT_ENCODED_XML };
		eo.content.encoded.typeId = UA_NODEID_NUMERIC( 0, 1 );
		eo.content.encoded.body = latin1;
		expect( scalarVariant(&eo, UA_TYPES[UA_TYPES_EXTENSIONOBJECT]), false, "ExtensionObject xml" );
		eo.encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
		expect( scalarVariant(&eo, UA_TYPES[UA_TYPES_EXTENSIONOBJECT]), true, "ExtensionObject binary" );

		UA_Variant elements[2]{ scalarVariant(&utf8, UA_TYPES[UA_TYPES_STRING]), scalarVariant(&latin1, UA_TYPES[UA_TYPES_STRING]) };
		expect( arrayVariant(elements, 2, UA_TYPES[UA_TYPES_VARIANT]), false, "Variant[]" );//a Variant array holding one.
		for( auto& e : elements )
			UA_Variant_clear( &e );
		EXPECT_FALSE( ProtoUtils::Utf8(sv{"25\xB0" "C"}) );
	}
}

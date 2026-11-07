//Structures (clients spec *Structures*, #421):  Value::ToJson walks a structure's members by its type's member list and Set walks them
//back - open62541's own types, and a server's own chained as a connection's registry chains them (ITypes) - an encoded ExtensionObject
//decodes by its encoding id against the chain, one no type explains stays opaque and is noted, and Variant::WithBuiltInTypes re-encodes a
//value of a chained type for what keeps it.
#include <gtest/gtest.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/uatypes/Variant.h>
#include <jde/opc/proto/opc.Common.h>
#include "helpers.h"
#define let const auto

namespace Jde::Opc::Tests{
	constexpr NsIndex _ns{ 2 };
	Ω field( const char* name, UA_NodeId dataType, bool array=false, bool optional=false )ι->UA_StructureField{
		UA_StructureField f; UA_StructureField_init( &f );
		f.name = UA_STRING( (char*)name );
		f.dataType = dataType;
		f.valueRank = array ? 1 : UA_VALUERANK_SCALAR;
		f.isOptional = optional;
		return f;
	}
	Ω field( const char* name, UA_UInt32 ns0Type, bool array=false, bool optional=false )ι->UA_StructureField{ return field( name, UA_NODEID_NUMERIC(0, ns0Type), array, optional ); }
	//A server's definition of a structure (UA_StructureDescription):  `id` in the test namespace, its DefaultBinary `id+1`.
	Ω description( UA_UInt32 id, const char* name, const vector<UA_StructureField>& fields, UA_StructureType kind=UA_STRUCTURETYPE_STRUCTURE )ι->UA_StructureDescription{
		UA_StructureDescription d; UA_StructureDescription_init( &d );
		d.dataTypeId = UA_NODEID_NUMERIC( _ns, id );
		d.name = UA_QUALIFIEDNAME( _ns, (char*)name );
		d.structureDefinition.defaultEncodingId = UA_NODEID_NUMERIC( _ns, id+1 );
		d.structureDefinition.baseDataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_STRUCTURE );
		d.structureDefinition.structureType = kind;
		d.structureDefinition.fields = (UA_StructureField*)fields.data();
		d.structureDefinition.fieldsSize = fields.size();
		return d;
	}
	//A registry of one array, each type built from its description against the ones before it, as a connection's is (Gateway::TypeRegistry).
	struct Registry final : ITypes{
		Registry( const vector<UA_StructureDescription>& descriptions )ε{
			_types.resize( descriptions.size() );
			_array = UA_DataTypeArray{ nullptr, 0, _types.data(), false };
			for( let& d : descriptions ){
				UA_ExtensionObject eo; UA_ExtensionObject_setValueNoDelete( &eo, (void*)&d, &UA_TYPES[UA_TYPES_STRUCTUREDESCRIPTION] );
				UAε( UA_DataType_fromDescription(&_types[_array.typesSize], &eo, &_array) );
				++_array.typesSize;
			}
		}
		~Registry(){
			for( auto& t : _types )
				UA_DataType_clear( &t );
		}
		α Chain()Ι->const UA_DataTypeArray* override{ return &_array; }
		α Missing( const UA_NodeId& id )Ι->void override{ Noted.push_back( NodeId{id} ); }
		α Type( uint i )Ι->const UA_DataType&{ return _types[i]; }
		mutable vector<NodeId> Noted;
	private:
		vector<UA_DataType> _types;
		UA_DataTypeArray _array{};
	};
	//`p` of `type` as it travels in a Variant:  an ExtensionObject of the type's DefaultBinary id and its binary form.
	Ω encoded( const void* p, const UA_DataType& type )ε->Value{
		UA_ExtensionObject eo; UA_ExtensionObject_init( &eo );
		eo.encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
		UAε( UA_NodeId_copy(&type.binaryEncodingId, &eo.content.encoded.typeId) );
		UAε( UA_encodeBinary(p, &type, &eo.content.encoded.body, nullptr) );
		Value y = dataValue( &eo, UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
		UA_ExtensionObject_clear( &eo );
		return y;
	}
	const vector<UA_StructureField> _sampleFields{ field("Rpm", UA_NS0ID_DOUBLE), field("Running", UA_NS0ID_BOOLEAN), field("Label", UA_NS0ID_STRING), field("Limits", UA_NS0ID_RANGE), field("History", UA_NS0ID_DOUBLE, true) };
	const vector<UA_StructureField> _nestedFields{ field("Sample", UA_NODEID_NUMERIC(_ns, 3001)), field("Count", UA_NS0ID_INT32) };
	Ω sampleJson()ι->jvalue{ return parse( R"({"Rpm":1480.5,"Running":true,"Label":"sample","Limits":{"Low":0E0,"High":3E3},"History":[1E0,2E0,3E0]})" ); }

	//A namespace-0 structure goes out as an object of its members by name, each in the shape its kind takes - a NodeId as {ns,i}, a
	//LocalizedText its text, an array member an array - where open62541's own encoder wrote Part 6's shapes.
	TEST( StructureTests, ANs0StructureWalksItsMembers ){
		const UA_Range range{ 0, 1500 };
		EXPECT_EQ( serialize(dataValue(&range, UA_TYPES[UA_TYPES_RANGE]).ToJson()), R"({"Low":0E0,"High":1.5E3})" );
		UA_Argument a; UA_Argument_init( &a );
		a.name = UA_STRING( (char*)"rpm" );
		a.dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_DOUBLE );
		a.valueRank = 1;
		UA_UInt32 dimensions[]{ 4 };
		a.arrayDimensions = dimensions; a.arrayDimensionsSize = 1;
		a.description = UA_LOCALIZEDTEXT( (char*)"en", (char*)"The speed." );
		let j = dataValue( &a, UA_TYPES[UA_TYPES_ARGUMENT] ).ToJson();
		let& o = j.as_object();
		EXPECT_EQ( o.at("Name").as_string(), "rpm" );
		EXPECT_EQ( NodeId{o.at("DataType")}, (NodeId{0, UA_NS0ID_DOUBLE}) );
		EXPECT_EQ( o.at("ValueRank").to_number<int>(), 1 );
		EXPECT_EQ( serialize(o.at("ArrayDimensions")), "[4]" );
		EXPECT_EQ( o.at("Description").as_string(), "The speed." ) << "the text alone, as every LocalizedText";
		EXPECT_EQ( o.size(), 5u ) << serialize( j );
	}
	//Set walks the object back by the same list, so a structure round-trips - a LocalizedText member from its text - and refuses a member
	//missing or of the wrong kind, naming it.  A member of a kind Set has no arm for yet, Argument's NodeId (clients plan, phase 9),
	//refuses the whole the same way.
	TEST( StructureTests, SetWalksTheMembersBack ){
		let range = Value{ parse(R"({"Low":1E0,"High":2E0})"), &UA_TYPES[UA_TYPES_RANGE] };
		EXPECT_EQ( range.Get<UA_Range>(0).high, 2 );
		EXPECT_EQ( serialize(range.ToJson()), R"({"Low":1E0,"High":2E0})" );
		let units = parse( R"({"NamespaceUri":"http://www.opcfoundation.org/UA/units/un/cefact","UnitId":4404,"DisplayName":"rpm","Description":"revolutions per minute"})" );
		let round = Value{ units, &UA_TYPES[UA_TYPES_EUINFORMATION] };
		EXPECT_EQ( round.Get<UA_EUInformation>(0).unitId, 4404 );
		EXPECT_EQ( ToSV(round.Get<UA_EUInformation>(0).displayName.text), "rpm" );
		EXPECT_EQ( round.ToJson(), units );
		EXPECT_THROW( Value( parse(R"({"Name":"rpm","DataType":{"i":11},"ValueRank":1,"ArrayDimensions":[],"Description":"d"})"), &UA_TYPES[UA_TYPES_ARGUMENT] ), Exception ) << "a NodeId member has no arm yet";
		EXPECT_THROW( Value( parse(R"({"Low":1E0})"), &UA_TYPES[UA_TYPES_RANGE] ), Exception ) << "High missing";
		EXPECT_THROW( Value( parse(R"({"Low":1E0,"High":"x"})"), &UA_TYPES[UA_TYPES_RANGE] ), Exception );
		EXPECT_THROW( Value( parse(R"([1,2])"), &UA_TYPES[UA_TYPES_RANGE] ), Exception ) << "not an object";
		try{ Value v{ parse(R"({"Low":1E0,"High":"x"})"), &UA_TYPES[UA_TYPES_RANGE] }; }
		catch( const Exception& e ){ EXPECT_NE( string{e.what()}.find("High"), string::npos ) << e.what(); }
		//An array of structures, as any array:  one per element.
		let ranges = Value{ parse(R"([{"Low":1E0,"High":2E0},{"Low":3E0,"High":4E0}])"), &UA_TYPES[UA_TYPES_RANGE] };
		ASSERT_EQ( ranges.value.arrayLength, 2u );
		EXPECT_EQ( ranges.Get<UA_Range>(1).low, 3 );
		EXPECT_EQ( serialize(ranges.ToJson()), R"([{"Low":1E0,"High":2E0},{"Low":3E0,"High":4E0}])" );
		EXPECT_TRUE( Value::Settable(UA_TYPES[UA_TYPES_RANGE]) );
		EXPECT_FALSE( Value::Settable(UA_TYPES[UA_TYPES_EXTENSIONOBJECT]) ) << "a body with no type is not a structure";
	}
	//An ExtensionObject decodes by its encoding id:  a namespace-0 body with no registry at all;  one of a type no registry explains stays
	//opaque, {typeId, body}, and is noted with the registry for its fetch - a namespace-0 one, which no fetch could explain, is not.
	TEST( StructureTests, AnEncodedBodyDecodesByItsEncoding ){
		const UA_Range range{ 0, 1500 };
		let eo = encoded( &range, UA_TYPES[UA_TYPES_RANGE] );
		EXPECT_EQ( serialize(eo.ToJson()), R"({"Low":0E0,"High":1.5E3})" );
		EXPECT_EQ( serialize(eo.ToJson(nullptr)), serialize(eo.ToJson()) );
		Registry registry{ {} };
		UA_DataType unknown = UA_TYPES[UA_TYPES_RANGE];//a Range travelling under an encoding id of the server's own.
		unknown.binaryEncodingId = UA_NODEID_NUMERIC( _ns, 777 );
		let opaque = encoded( &range, unknown ).ToJson( &registry );
		ASSERT_TRUE( opaque.is_object() ) << serialize( opaque );
		EXPECT_EQ( NodeId{opaque.at("typeId")}, (NodeId{_ns, 777}) );
		EXPECT_EQ( opaque.at("body").as_string(), "AAAAAAAAAAAAAAAAAHCXQA==" ) << "the body, base64";
		ASSERT_EQ( registry.Noted.size(), 1u );
		EXPECT_EQ( registry.Noted[0], (NodeId{_ns, 777}) );
		unknown.binaryEncodingId = UA_NODEID_NUMERIC( 0, 99999 );
		EXPECT_TRUE( encoded(&range, unknown).ToJson(&registry).as_object().contains("body") );
		EXPECT_EQ( registry.Noted.size(), 1u ) << "namespace 0's are never fetched";
	}
	//A server's own type, built from its definition as the registry builds them:  a value of it writes from the object and reads back as
	//it, decoded or encoded, a type with a member of another of the server's own nesting it.
	TEST( StructureTests, AServersOwnTypeDecodesAndWritesBack ){
		Registry registry{ {description(3001, "PumpSampleType", _sampleFields), description(3003, "NestedType", _nestedFields)} };
		let& sample = registry.Type( 0 );
		EXPECT_EQ( sample.typeKind, UA_DATATYPEKIND_STRUCTURE );
		EXPECT_TRUE( Value::Settable(sample) );
		let json = sampleJson();
		Value v{ json, &sample };
		EXPECT_EQ( v.ToJson(&registry), json );
		EXPECT_EQ( v.ToJson(), json ) << "a decoded value walks by its own type:  the registry is for an encoded body";
		EXPECT_EQ( encoded(v.value.data, sample).ToJson(&registry), json ) << "the body decoded against the chain";
		EXPECT_TRUE( encoded(v.value.data, sample).ToJson().as_object().contains("body") ) << "opaque without the registry";
		let nested = parse( R"({"Sample":{"Rpm":1E0,"Running":false,"Label":"n","Limits":{"Low":0E0,"High":1E0},"History":[]},"Count":7})" );
		Value n{ nested, &registry.Type(1) };
		EXPECT_EQ( n.ToJson(&registry), nested );
		EXPECT_EQ( encoded(n.value.data, registry.Type(1)).ToJson(&registry), nested );
		EXPECT_THROW( Value( parse(R"({"Sample":5,"Count":7})"), &registry.Type(1) ), Exception );
	}
	//An optional member absent goes out as null and reads back absent;  a union goes out as its one selected member and reads back from it,
	//none as an empty object, and two members or a name it has none of are refused.
	TEST( StructureTests, OptionalMembersAndUnions ){
		const vector<UA_StructureField> optFields{ field("A", UA_NS0ID_DOUBLE), field("B", UA_NS0ID_STRING, false, true), field("C", UA_NS0ID_INT32, true, true) };
		const vector<UA_StructureField> unionFields{ field("U", UA_NS0ID_DOUBLE), field("V", UA_NS0ID_STRING), field("W", UA_NS0ID_INT32, true) };
		Registry registry{ {description(3005, "OptType", optFields, UA_STRUCTURETYPE_STRUCTUREWITHOPTIONALFIELDS), description(3007, "UnionType", unionFields, UA_STRUCTURETYPE_UNION)} };
		let& opt = registry.Type( 0 ); let& u = registry.Type( 1 );
		EXPECT_EQ( opt.typeKind, UA_DATATYPEKIND_OPTSTRUCT );
		EXPECT_EQ( u.typeKind, UA_DATATYPEKIND_UNION );
		EXPECT_EQ( serialize(Value(parse(R"({"A":1E0})"), &opt).ToJson()), R"({"A":1E0,"B":null,"C":null})" );
		let full = parse( R"({"A":1E0,"B":"b","C":[1,2]})" );
		EXPECT_EQ( Value(full, &opt).ToJson(), full );
		EXPECT_EQ( encoded(Value(full, &opt).value.data, opt).ToJson(&registry), full );
		EXPECT_EQ( Value(parse(R"({"A":1E0,"B":null})"), &opt).ToJson(), parse(R"({"A":1E0,"B":null,"C":null})") ) << "null is absent";
		EXPECT_THROW( Value( parse(R"({"B":"b"})"), &opt ), Exception ) << "A is not optional";
		EXPECT_EQ( serialize(Value(parse(R"({"V":"x"})"), &u).ToJson()), R"({"V":"x"})" );
		EXPECT_EQ( serialize(Value(parse(R"({"W":[3]})"), &u).ToJson()), R"({"W":[3]})" );
		EXPECT_EQ( serialize(Value(parse("{}"), &u).ToJson()), "{}" );
		EXPECT_EQ( encoded(Value(parse(R"({"U":2E0})"), &u).value.data, u).ToJson(&registry), parse(R"({"U":2E0})") );
		EXPECT_THROW( Value( parse(R"({"U":1E0,"V":"x"})"), &u ), Exception );
		EXPECT_THROW( Value( parse(R"({"X":1E0})"), &u ), Exception );
	}
	//What keeps a value past the request takes it with built-in types alone:  a structure of a registry's type re-encoded as the
	//ExtensionObject it travelled as, a scalar or every element of an array, a decoded one inside an ExtensionObject or a Variant element
	//the same, and a built-in value left as it is.
	TEST( StructureTests, WithBuiltInTypesReEncodesARegistryType ){
		Registry registry{ {description(3001, "PumpSampleType", _sampleFields)} };
		let& sample = registry.Type( 0 );
		Value v{ sampleJson(), &sample };
		let scalar = Variant::WithBuiltInTypes( v.value );
		ASSERT_TRUE( scalar );
		ASSERT_TRUE( UA_Variant_hasScalarType(scalar.operator->(), &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]) );
		let& eo = *(const UA_ExtensionObject*)scalar->data;
		EXPECT_EQ( eo.encoding, UA_EXTENSIONOBJECT_ENCODED_BYTESTRING );
		EXPECT_EQ( NodeId{eo.content.encoded.typeId}, (NodeId{_ns, 3002}) ) << "the DefaultBinary id";
		EXPECT_EQ( Value(Variant{*scalar}.Move()).ToJson(&registry), sampleJson() ) << "the same value, decoded again";
		Value array{ parse(Ƒ("[{},{}]", serialize(sampleJson()), serialize(sampleJson()))), &sample };
		let elements = Variant::WithBuiltInTypes( array.value );
		ASSERT_TRUE( elements );
		EXPECT_EQ( elements->arrayLength, 2u );
		EXPECT_EQ( elements->type, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
		const UA_Double d{ 1.5 };
		EXPECT_FALSE( Variant::WithBuiltInTypes(Variant{scalarVariant(&d, UA_TYPES[UA_TYPES_DOUBLE])}) ) << "built in:  kept as it is";
		const UA_Range range{ 0, 1 };
		EXPECT_FALSE( Variant::WithBuiltInTypes(Variant{scalarVariant(&range, UA_TYPES[UA_TYPES_RANGE])}) ) << "open62541's own structure:  kept";
		UA_ExtensionObject decoded; UA_ExtensionObject_setValueNoDelete( &decoded, v.value.data, &sample );
		let inner = Variant::WithBuiltInTypes( Variant{scalarVariant(&decoded, UA_TYPES[UA_TYPES_EXTENSIONOBJECT])} );
		ASSERT_TRUE( inner );
		EXPECT_EQ( ((const UA_ExtensionObject*)inner->data)->encoding, UA_EXTENSIONOBJECT_ENCODED_BYTESTRING );
	}
	//A registry's type held anywhere a namespace-0 value can hold one is re-encoded too (reviews-issue/421-type-registry.md #11):  a
	//KeyValuePair's Value, every element of an array of them, a DataValue's value inside a Variant - and a KeyValuePair of built-in values
	//is left as it is.
	TEST( StructureTests, WithBuiltInTypesReachesNestedValues ){
		Registry registry{ {description(3001, "PumpSampleType", _sampleFields)} };
		let& sample = registry.Type( 0 );
		Value v{ sampleJson(), &sample };
		let reencoded = []( const UA_Variant& x )ι->bool{ return x.type==&UA_TYPES[UA_TYPES_EXTENSIONOBJECT] && ((const UA_ExtensionObject*)x.data)->encoding==UA_EXTENSIONOBJECT_ENCODED_BYTESTRING; };
		UA_KeyValuePair pairs[2]; UA_KeyValuePair_init( &pairs[0] ); UA_KeyValuePair_init( &pairs[1] );
		pairs[0].key = UA_QUALIFIEDNAME( 0, (char*)"sample" );
		UA_Variant_setScalar( &pairs[0].value, v.value.data, &sample );//borrowed:  arrayVariant copies.
		pairs[1].key = UA_QUALIFIEDNAME( 0, (char*)"double" );
		const UA_Double d{ 1.5 };
		UA_Variant_setScalar( &pairs[1].value, (void*)&d, &UA_TYPES[UA_TYPES_DOUBLE] );
		const Variant kvs{ arrayVariant(pairs, 2, UA_TYPES[UA_TYPES_KEYVALUEPAIR]) };
		let kept = Variant::WithBuiltInTypes( kvs );
		ASSERT_TRUE( kept );
		let& keptPairs = *(const UA_KeyValuePair(*)[2])kept->data;
		EXPECT_TRUE( reencoded(keptPairs[0].value) ) << "the sample, as it travelled";
		EXPECT_EQ( keptPairs[1].value.type, &UA_TYPES[UA_TYPES_DOUBLE] ) << "built in:  as it was";
		EXPECT_EQ( Value(Variant{*kept}.Move()).ToJson(&registry).as_array()[0].as_object().at("Value"), sampleJson() ) << "the same value, decoded again";
		EXPECT_FALSE( Variant::WithBuiltInTypes(Variant{scalarVariant(&pairs[1], UA_TYPES[UA_TYPES_KEYVALUEPAIR])}) );

		UA_DataValue dv; UA_DataValue_init( &dv );
		UA_Variant_setScalar( &dv.value, v.value.data, &sample ); dv.hasValue = true;
		let inDataValue = Variant::WithBuiltInTypes( Variant{scalarVariant(&dv, UA_TYPES[UA_TYPES_DATAVALUE])} );
		ASSERT_TRUE( inDataValue );
		EXPECT_TRUE( reencoded(((const UA_DataValue*)inDataValue->data)->value) );
	}
	//A Variant element or member is its value, walked as a top-level one is (#9):  a decoded registry type as its fields, an encoded one
	//decoded against the chain, one no type explains opaque and noted - by the serializer and by the data-change callback's NoteMissingTypes.
	TEST( StructureTests, AVariantElementWalksItsValue ){
		Registry registry{ {description(3001, "PumpSampleType", _sampleFields)} };
		let& sample = registry.Type( 0 );
		Value v{ sampleJson(), &sample };
		const Value encodedSample = encoded( v.value.data, sample );
		UA_ExtensionObject unknown; UA_ExtensionObject_init( &unknown );
		unknown.encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
		unknown.content.encoded.typeId = UA_NODEID_NUMERIC( _ns, 888 );
		UA_Variant elements[3]; UA_Variant_init( &elements[0] ); UA_Variant_init( &elements[1] ); UA_Variant_init( &elements[2] );
		UA_Variant_setScalar( &elements[0], v.value.data, &sample );//borrowed:  arrayVariant copies.
		elements[1] = encodedSample.value;
		UA_Variant_setScalar( &elements[2], &unknown, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
		const Value array{ arrayVariant(elements, 3, UA_TYPES[UA_TYPES_VARIANT]) };
		let json = array.ToJson( &registry );
		ASSERT_TRUE( json.is_array() ) << serialize( json );
		EXPECT_EQ( json.as_array()[0], sampleJson() );
		EXPECT_EQ( json.as_array()[1], sampleJson() );
		EXPECT_TRUE( json.as_array()[2].as_object().contains("body") ) << serialize( json );
		ASSERT_EQ( registry.Noted.size(), 1u );
		EXPECT_EQ( registry.Noted[0], (NodeId{_ns, 888}) );
		NoteMissingTypes( array.value, registry );
		ASSERT_EQ( registry.Noted.size(), 2u ) << "the callback's note reaches a Variant element";
		EXPECT_EQ( registry.Noted[1], (NodeId{_ns, 888}) );
	}

	//The proto's two forms of a structure (Opc.Common.proto, clients plan 8b):  Encoded, the ExtensionObject it travels as, which the
	//historian stores;  Decoded, its members by name as `structure`, nested where the members are, an array member an array - a namespace-0
	//structure with no registry, a registry's by the registry, decoded or encoded - and ToVariant rebuilds either, the structure member by
	//member as the type its id names.
	Ω member( const Proto::Structure& s, sv name )->const Proto::Value*{
		for( let& m : s.members() ){
			if( m.name()==name )
				return m.has_value() ? &m.value() : nullptr;
		}
		ADD_FAILURE() << "no member " << name;
		return nullptr;
	}
	Ω names( const Proto::Structure& s )->vector<string>{ vector<string> y; for( let& m : s.members() ) y.push_back( m.name() ); return y; }
	TEST( StructureTests, ProtoCarriesAStructureAsItsMembers ){
		using enum ProtoUtils::EStructures;
		const UA_Range range{ 0, 1500 };
		const Value v = dataValue( &range, UA_TYPES[UA_TYPES_RANGE] );
		EXPECT_TRUE( ProtoUtils::ToValue(v.value).has_extension_object() ) << "Encoded by default:  what the historian stores";
		let decoded = ProtoUtils::ToValue( v.value, Decoded );
		ASSERT_TRUE( decoded.has_structure() );
		EXPECT_EQ( ProtoUtils::ToNodeId(decoded.structure().type_id()), (NodeId{0, UA_NS0ID_RANGE}) ) << "the DataType's id";
		EXPECT_EQ( names(decoded.structure()), (vector<string>{"Low", "High"}) );
		EXPECT_EQ( member(decoded.structure(), "High")->double_value(), 1500 );
		let back = ProtoUtils::ToVariant( decoded );
		ASSERT_EQ( back.type, &UA_TYPES[UA_TYPES_RANGE] ) << "rebuilt as a Range, not an ExtensionObject";
		EXPECT_EQ( ((const UA_Range*)back.data)->high, 1500 );
		EXPECT_EQ( ProtoUtils::ToValue(back, Decoded).SerializeAsString(), decoded.SerializeAsString() );

		Registry registry{ {description(3001, "PumpSampleType", _sampleFields), description(3003, "NestedType", _nestedFields)} };
		let& sample = registry.Type( 0 );
		Value s{ sampleJson(), &sample };
		let proto = ProtoUtils::ToValue( s.value, Decoded, &registry );
		ASSERT_TRUE( proto.has_structure() );
		EXPECT_EQ( ProtoUtils::ToNodeId(proto.structure().type_id()), (NodeId{_ns, 3001}) );
		EXPECT_EQ( names(proto.structure()), (vector<string>{"Rpm", "Running", "Label", "Limits", "History"}) );
		EXPECT_EQ( member(proto.structure(), "Rpm")->double_value(), 1480.5 );
		EXPECT_EQ( member(proto.structure(), "Label")->string_value(), "sample" );
		ASSERT_TRUE( member(proto.structure(), "Limits")->has_structure() ) << "a namespace-0 member, nested";
		EXPECT_EQ( member(member(proto.structure(), "Limits")->structure(), "High")->double_value(), 3000 );
		ASSERT_TRUE( member(proto.structure(), "History")->has_array() );
		EXPECT_EQ( member(proto.structure(), "History")->array().values_size(), 3 );
		EXPECT_EQ( member(proto.structure(), "History")->array().type(), UA_DATATYPEKIND_DOUBLE+1 );
		EXPECT_EQ( ProtoUtils::ToValue(s.value, Decoded).SerializeAsString(), proto.SerializeAsString() ) << "a decoded value walks by the type it holds:  the registry is for an encoded body";
		EXPECT_EQ( ProtoUtils::ToValue(encoded(s.value.data, sample).value, Decoded, &registry).SerializeAsString(), proto.SerializeAsString() ) << "an encoded body, by the registry";
		let stored = ProtoUtils::ToValue( s.value );
		ASSERT_TRUE( stored.has_extension_object() ) << "Encoded:  as the historian stores it";
		EXPECT_EQ( ProtoUtils::ToNodeId(stored.extension_object().type_id()), (NodeId{_ns, 3002}) ) << "the DefaultBinary id";
		auto onto = stored;
		ProtoUtils::DecodeStructures( onto, &registry );
		EXPECT_EQ( onto.SerializeAsString(), proto.SerializeAsString() ) << "a stored body onto the socket";
		auto unknown = stored;
		ProtoUtils::DecodeStructures( unknown, nullptr );
		EXPECT_TRUE( unknown.has_extension_object() ) << "no registry:  kept as it came";
		EXPECT_THROW( ProtoUtils::ToVariant(proto), UAException ) << "no registry explains the type";
		let rebuilt = ProtoUtils::ToVariant( proto, &registry );
		ASSERT_EQ( rebuilt.type, &sample );
		EXPECT_EQ( Value{Variant{rebuilt}.Move()}.ToJson(&registry), sampleJson() ) << "member by member, the nested Range and the array included";
		//An array of structures goes as an array of them, its elements' type the ExtensionObject they travel as.
		Value array{ parse(Ƒ("[{},{}]", serialize(sampleJson()), serialize(sampleJson()))), &sample };
		let elements = ProtoUtils::ToValue( array.value, Decoded, &registry );
		ASSERT_TRUE( elements.has_array() );
		EXPECT_EQ( elements.array().type(), UA_DATATYPEKIND_EXTENSIONOBJECT+1 );
		EXPECT_TRUE( elements.array().values(1).has_structure() );
		let arrayBack = ProtoUtils::ToVariant( elements, &registry );
		EXPECT_EQ( arrayBack.type, &sample );
		EXPECT_EQ( arrayBack.arrayLength, 2u );
		//A type nesting another of the registry's.
		let nested = parse( R"({"Sample":{"Rpm":1E0,"Running":false,"Label":"n","Limits":{"Low":0E0,"High":1E0},"History":[]},"Count":7})" );
		Value n{ nested, &registry.Type(1) };
		let nestedProto = ProtoUtils::ToValue( n.value, Decoded, &registry );
		ASSERT_TRUE( member(nestedProto.structure(), "Sample")->has_structure() );
		EXPECT_EQ( Value{Variant{ProtoUtils::ToVariant(nestedProto, &registry)}.Move()}.ToJson(&registry), nested );
	}
	//A body no type explains stays an extension_object, noted with the registry;  a structure whose member is missing, misnamed or of the
	//wrong kind is refused on the way back;  an absent optional member goes with no value and comes back absent, and a union as its one
	//selected member.
	TEST( StructureTests, ProtoOpaqueOptionalAndUnion ){
		using enum ProtoUtils::EStructures;
		Registry registry{ {description(3001, "PumpSampleType", _sampleFields)} };
		let& sample = registry.Type( 0 );
		UA_DataType unknown = UA_TYPES[UA_TYPES_RANGE];
		unknown.binaryEncodingId = UA_NODEID_NUMERIC( _ns, 777 );
		const UA_Range range{ 0, 1500 };
		let opaque = ProtoUtils::ToValue( encoded(&range, unknown).value, Decoded, &registry );
		ASSERT_TRUE( opaque.has_extension_object() );
		EXPECT_EQ( ProtoUtils::ToNodeId(opaque.extension_object().type_id()), (NodeId{_ns, 777}) );
		ASSERT_EQ( registry.Noted.size(), 1u );
		EXPECT_EQ( registry.Noted[0], (NodeId{_ns, 777}) );
		EXPECT_EQ( ProtoUtils::ToVariant(opaque, &registry).type, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] ) << "back as it came";

		Value s{ sampleJson(), &sample };
		let proto = ProtoUtils::ToValue( s.value, Decoded, &registry );
		auto missing = proto; missing.mutable_structure()->mutable_members()->DeleteSubrange( 2, 1 );
		EXPECT_THROW( ProtoUtils::ToVariant(missing, &registry), UAException ) << "Label missing";
		auto misnamed = proto; misnamed.mutable_structure()->mutable_members(2)->set_name( "Labelled" );
		EXPECT_THROW( ProtoUtils::ToVariant(misnamed, &registry), UAException );
		auto wrong = proto; wrong.mutable_structure()->mutable_members(0)->mutable_value()->set_string_value( "fast" );
		EXPECT_THROW( ProtoUtils::ToVariant(wrong, &registry), UAException ) << "Rpm is a Double";
		auto scalar = proto; scalar.mutable_structure()->mutable_members(4)->mutable_value()->set_double_value( 1 );
		EXPECT_THROW( ProtoUtils::ToVariant(scalar, &registry), UAException ) << "History is an array";
		try{ ProtoUtils::ToVariant( wrong, &registry ); }
		catch( const UAException& e ){ EXPECT_NE( string{e.what()}.find("Rpm"), string::npos ) << e.what(); }

		const vector<UA_StructureField> optFields{ field("A", UA_NS0ID_DOUBLE), field("B", UA_NS0ID_STRING, false, true), field("C", UA_NS0ID_INT32, true, true) };
		const vector<UA_StructureField> unionFields{ field("U", UA_NS0ID_DOUBLE), field("V", UA_NS0ID_STRING), field("W", UA_NS0ID_INT32, true) };
		Registry more{ {description(3005, "OptType", optFields, UA_STRUCTURETYPE_STRUCTUREWITHOPTIONALFIELDS), description(3007, "UnionType", unionFields, UA_STRUCTURETYPE_UNION)} };
		let& opt = more.Type( 0 ); let& u = more.Type( 1 );
		let partial = ProtoUtils::ToValue( Value(parse(R"({"A":1E0})"), &opt).value, Decoded, &more );
		ASSERT_TRUE( partial.has_structure() );
		EXPECT_EQ( names(partial.structure()), (vector<string>{"A", "B", "C"}) );
		EXPECT_FALSE( partial.structure().members(1).has_value() ) << "absent:  no value";
		EXPECT_FALSE( partial.structure().members(2).has_value() );
		EXPECT_EQ( serialize(Value{Variant{ProtoUtils::ToVariant(partial, &more)}.Move()}.ToJson()), R"({"A":1E0,"B":null,"C":null})" );
		let full = parse( R"({"A":1E0,"B":"b","C":[1,2]})" );
		EXPECT_EQ( Value{Variant{ProtoUtils::ToVariant(ProtoUtils::ToValue(Value(full, &opt).value, Decoded, &more), &more)}.Move()}.ToJson(), full );
		let selected = ProtoUtils::ToValue( Value(parse(R"({"W":[3]})"), &u).value, Decoded, &more );
		EXPECT_EQ( names(selected.structure()), vector<string>{"W"} ) << "the selected member alone";
		EXPECT_EQ( serialize(Value{Variant{ProtoUtils::ToVariant(selected, &more)}.Move()}.ToJson()), R"({"W":[3]})" );
		let none = ProtoUtils::ToValue( Value(parse("{}"), &u).value, Decoded, &more );
		EXPECT_TRUE( none.structure().members().empty() );
		EXPECT_EQ( serialize(Value{Variant{ProtoUtils::ToVariant(none, &more)}.Move()}.ToJson()), "{}" );
		auto two = selected; auto& v = *two.mutable_structure()->add_members(); v.set_name( "V" ); v.mutable_value()->set_string_value( "x" );
		EXPECT_THROW( ProtoUtils::ToVariant(two, &more), UAException ) << "one of its members, not two";
	}
	//What a member's kind asks of the two forms (reviews-issue/422-structures.md #1-#5, #12):  a structure with a member of no proto form,
	//or text that isn't UTF-8, goes as its body - the nearest structure holding it alone - where it used to end the process;  a null array
	//member, a BaseDataType member, a Structure member and a Structure[] of two types come back as they went;  a value of another kind the
	//size of the member's is refused.
	TEST( StructureTests, ProtoMembersOfEveryKind ){
		using enum ProtoUtils::EStructures;
		UA_StatusResult result; UA_StatusResult_init( &result );
		result.statusCode = UA_STATUSCODE_BADINTERNALERROR;
		let unsupported = ProtoUtils::ToValue( &result, UA_TYPES[UA_TYPES_STATUSRESULT], Decoded );
		ASSERT_TRUE( unsupported.has_extension_object() ) << "a DiagnosticInfo member has no proto form:  the body";
		EXPECT_EQ( ProtoUtils::ToNodeId(unsupported.extension_object().type_id()), NodeId{UA_TYPES[UA_TYPES_STATUSRESULT].binaryEncodingId} );
		const vector<UA_StructureField> resultFields{ field("Count", UA_NS0ID_INT32), field("Result", UA_NS0ID_STATUSRESULT) };
		const vector<UA_StructureField> readingFields{ field("Reading", UA_NS0ID_DATAVALUE) };
		Registry registry{ {description(3009, "ResultHolder", resultFields), description(3011, "ReadingHolder", readingFields)} };
		for( uint i : {0, 1} ){
			let& type = registry.Type( i );
			auto p = UA_new( &type );
			let proto = ProtoUtils::ToValue( p, type, Decoded, &registry );
			UA_delete( p, &type );
			if( i==0 ){
				ASSERT_TRUE( proto.has_structure() ) << "the structure around it stays decoded";
				EXPECT_TRUE( member(proto.structure(), "Result")->has_extension_object() );
			}
			else
				EXPECT_TRUE( proto.has_extension_object() ) << "a DataValue member";
		}

		UA_KeyValuePair pair; UA_KeyValuePair_init( &pair );
		pair.key = UA_QUALIFIEDNAME( 1, (char*)"speed" );
		const UA_Double speed{ 3.5 };
		UA_Variant_setScalar( &pair.value, (void*)&speed, &UA_TYPES[UA_TYPES_DOUBLE] );
		let pairProto = ProtoUtils::ToValue( &pair, UA_TYPES[UA_TYPES_KEYVALUEPAIR], Decoded );
		ASSERT_TRUE( pairProto.has_structure() );
		EXPECT_EQ( member(pairProto.structure(), "Value")->double_value(), 3.5 ) << "a BaseDataType member goes as what it holds";
		let pairBack = ProtoUtils::ToVariant( pairProto );
		ASSERT_EQ( pairBack.type, &UA_TYPES[UA_TYPES_KEYVALUEPAIR] );
		let& backPair = *(const UA_KeyValuePair*)pairBack.data;
		ASSERT_EQ( backPair.value.type, &UA_TYPES[UA_TYPES_DOUBLE] ) << "back in its Variant";
		EXPECT_EQ( *(const UA_Double*)backPair.value.data, 3.5 );
		UA_Variant_init( &pair.value );
		let emptyBack = ProtoUtils::ToVariant( ProtoUtils::ToValue(&pair, UA_TYPES[UA_TYPES_KEYVALUEPAIR], Decoded) );
		EXPECT_TRUE( UA_Variant_isEmpty(&((const UA_KeyValuePair*)emptyBack.data)->value) ) << "an empty one too";

		UA_Argument argument; UA_Argument_init( &argument );
		argument.name = UA_STRING( (char*)"rate" );
		argument.dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_DOUBLE );
		argument.valueRank = UA_VALUERANK_SCALAR;
		let argumentProto = ProtoUtils::ToValue( &argument, UA_TYPES[UA_TYPES_ARGUMENT], Decoded );
		ASSERT_TRUE( member(argumentProto.structure(), "ArrayDimensions")->array().is_null() );
		let argumentBack = ProtoUtils::ToVariant( argumentProto );
		ASSERT_EQ( argumentBack.type, &UA_TYPES[UA_TYPES_ARGUMENT] ) << "a null array member, as a server sends one it never set";
		EXPECT_EQ( ((const UA_Argument*)argumentBack.data)->arrayDimensions, nullptr );
		auto guid = argumentProto; guid.mutable_structure()->mutable_members(0)->mutable_value()->set_guid( string(16, '\x01') );
		EXPECT_THROW( ProtoUtils::ToVariant(guid), UAException ) << "a Guid is a String's size, not a String";
		auto qualified = argumentProto; qualified.mutable_structure()->mutable_members(1)->mutable_value()->mutable_qualified_name()->set_name( "x" );
		EXPECT_THROW( ProtoUtils::ToVariant(qualified), UAException ) << "a QualifiedName is a NodeId's size, not a NodeId";
		const UA_Range range{ 0, 1500 };
		auto int64 = ProtoUtils::ToValue( &range, UA_TYPES[UA_TYPES_RANGE], Decoded );
		int64.mutable_structure()->mutable_members(0)->mutable_value()->set_int64( 7 );
		EXPECT_THROW( ProtoUtils::ToVariant(int64), UAException ) << "an Int64 is a Double's size, not a Double";
		const char bad[]{ "\xff\xfe" };
		argument.name = UA_String{ 2, (UA_Byte*)bad };
		EXPECT_TRUE( ProtoUtils::ToValue(&argument, UA_TYPES[UA_TYPES_ARGUMENT], Decoded).has_extension_object() ) << "text that isn't UTF-8 goes in the body's bytes";
		EXPECT_THROW( ProtoUtils::ToValue(&argument.name, UA_TYPES[UA_TYPES_STRING], Decoded), UAException ) << "refused, as the historian stores it";
		EXPECT_EQ( ProtoUtils::ToValue(&argument.name, UA_TYPES[UA_TYPES_STRING]).string_value().size(), 2u ) << "Encoded:  the historian's to check";

		UA_DataChangeFilter filter; UA_DataChangeFilter_init( &filter );
		filter.trigger = UA_DATACHANGETRIGGER_STATUSVALUE;
		filter.deadbandType = UA_DEADBANDTYPE_ABSOLUTE;
		filter.deadbandValue = 0.5;
		UA_MonitoringParameters parameters; UA_MonitoringParameters_init( &parameters );
		parameters.samplingInterval = 250;
		parameters.queueSize = 4;
		UA_ExtensionObject_setValueNoDelete( &parameters.filter, &filter, &UA_TYPES[UA_TYPES_DATACHANGEFILTER] );
		let parametersProto = ProtoUtils::ToValue( &parameters, UA_TYPES[UA_TYPES_MONITORINGPARAMETERS], Decoded );
		ASSERT_TRUE( member(parametersProto.structure(), "Filter")->has_structure() ) << "a Structure member goes as the structure it holds";
		let parametersBack = ProtoUtils::ToVariant( parametersProto );
		ASSERT_EQ( parametersBack.type, &UA_TYPES[UA_TYPES_MONITORINGPARAMETERS] );
		let& backFilter = ((const UA_MonitoringParameters*)parametersBack.data)->filter;
		ASSERT_EQ( backFilter.encoding, UA_EXTENSIONOBJECT_DECODED ) << "back in its ExtensionObject";
		ASSERT_EQ( backFilter.content.decoded.type, &UA_TYPES[UA_TYPES_DATACHANGEFILTER] );
		EXPECT_EQ( ((const UA_DataChangeFilter*)backFilter.content.decoded.data)->deadbandValue, 0.5 );
		EXPECT_EQ( ((const UA_DataChangeFilter*)backFilter.content.decoded.data)->trigger, UA_DATACHANGETRIGGER_STATUSVALUE ) << "an enum member from its Int32";

		UA_ElementOperand element{ 1 };
		UA_LiteralOperand literal; UA_LiteralOperand_init( &literal );
		const UA_Double limit{ 3.5 };
		UA_Variant_setScalar( &literal.value, (void*)&limit, &UA_TYPES[UA_TYPES_DOUBLE] );
		UA_ExtensionObject operands[2];
		UA_ExtensionObject_setValueNoDelete( &operands[0], &element, &UA_TYPES[UA_TYPES_ELEMENTOPERAND] );
		UA_ExtensionObject_setValueNoDelete( &operands[1], &literal, &UA_TYPES[UA_TYPES_LITERALOPERAND] );
		UA_ContentFilterElement where; UA_ContentFilterElement_init( &where );
		where.filterOperator = UA_FILTEROPERATOR_GREATERTHAN;
		where.filterOperandsSize = 2;
		where.filterOperands = operands;
		let whereBack = ProtoUtils::ToVariant( ProtoUtils::ToValue(&where, UA_TYPES[UA_TYPES_CONTENTFILTERELEMENT], Decoded) );
		ASSERT_EQ( whereBack.type, &UA_TYPES[UA_TYPES_CONTENTFILTERELEMENT] ) << "a Structure[] of two types";
		let& backWhere = *(const UA_ContentFilterElement*)whereBack.data;
		ASSERT_EQ( backWhere.filterOperandsSize, 2u );
		EXPECT_EQ( backWhere.filterOperands[0].content.decoded.type, &UA_TYPES[UA_TYPES_ELEMENTOPERAND] );
		ASSERT_EQ( backWhere.filterOperands[1].content.decoded.type, &UA_TYPES[UA_TYPES_LITERALOPERAND] );
		EXPECT_EQ( *(const UA_Double*)((const UA_LiteralOperand*)backWhere.filterOperands[1].content.decoded.data)->value.data, 3.5 );
	}
}
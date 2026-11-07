//Structures (clients spec *Structures*, #421):  Value::ToJson walks a structure's members by its type's member list and Set walks them
//back - open62541's own types, and a server's own chained as a connection's registry chains them (ITypes) - an encoded ExtensionObject
//decodes by its encoding id against the chain, one no type explains stays opaque and is noted, and Variant::WithBuiltInTypes re-encodes a
//value of a chained type for what keeps it.
#include <gtest/gtest.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/uatypes/Variant.h>
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
}
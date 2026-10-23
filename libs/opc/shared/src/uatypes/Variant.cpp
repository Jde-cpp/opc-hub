#include <jde/opc/uatypes/Variant.h>
#include <charconv>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/UAString.h>
#include <jde/opc/uatypes/Value.h>

#define let const auto
namespace Jde::Opc{
	//Value::Set already decodes a jvalue toward a declared UA type, and infers one when there is none - this delegates
	//rather than growing a second conversion chain that can drift from it.  Previously only the literal "double" was
	//honoured and everything else fell through to json inference, so a config's {dataType:"int", value:5} produced an
	//Int64 against an Int32 attribute and UA_Server_addVariableNode rejected the node with BadTypeMismatch.
	Variant::Variant( const jvalue& v, const UA_DataType* dataType )ε:
		Owned{ move(Value{v, dataType}.value) }
	{}

	//OPC-UA Part 6 spells the non-finite doubles as the strings "NaN"/"Infinity"/"-Infinity", which is what UA_encodeJson
	//writes.  Boost.JSON's default options write a NaN as `null`, the same text a value-less reading serializes as, so a
	//failed sensor - NaN is the common one - reached the SPA as a blank tag.  Its allow_infinity_and_nan writes the bare
	//tokens NaN/Infinity instead, which are not json at all: JSON.parse throws on the whole response.  The string spelling
	//is valid json, is the vendor's, and is distinguishable from "no value".  (±Inf survived by accident - Boost writes
	//1e99999, which JSON.parse reads as Infinity - but it goes the same way, so the three stay one shape.)
	Ω toJson( double v )ι->jvalue{
		return std::isnan( v ) ? jvalue{"NaN"} : std::isinf( v ) ? jvalue{ v<0 ? "-Infinity" : "Infinity" } : jvalue{v};
	}
	//A Float at the fewest digits that are still that float.  Widened as it is, 21.3f writes 21.299999237060547, which every screen
	//showed;  the double parsed from the float's shortest text writes 21.3, and narrows back to the same float.
	Ω toJson( float v )ι->jvalue{
		if( !std::isfinite(v) )
			return toJson( (double)v );
		char text[32];
		const char* end = std::to_chars( text, text+sizeof(text), v ).ptr;
		double y{};
		std::from_chars( text, end, y );
		return jvalue{ y };
	}
	template<class N> Ω toJson( N v )ι->jvalue{
		if constexpr( std::is_same_v<N,UA_Int64> || std::is_same_v<N,UA_UInt64> )
			return Opc::ToJson( v );//the protobufjs Long {high,low,unsigned}:  a double cannot hold every 64-bit value.
		else
			return jvalue{ v };
	}

	Ω encoded( const void* element, const UA_DataType& type )ε->jvalue{
		UAString ua;
		UA_EncodeJsonOptions options{};
		if( let sc=UA_encodeJson(element, &type, &ua, &options); sc )
			throw UAException{ sc };
		try{
			return parse( ToSV(ua) );//off the encoder's buffer: the std::string was only ever an intermediate here.
		}
		catch( runtime_error& e ){
			let uaJson = ua.ToString();//only the failure path needs it as a string.
			ERRT( ELogTags::Parsing, "Error parsing {} - {}", uaJson, e.what() );
			return {uaJson};
		}
	}

	α Variant::ElementToJson( const void* element, const UA_DataType& type )ε->jvalue{
		switch( type.typeKind ){
		case UA_DATATYPEKIND_STRING:
		case UA_DATATYPEKIND_XMLELEMENT: return jstring{ ToSV(*(const UA_String*)element) };
		case UA_DATATYPEKIND_BYTESTRING: return ByteStringToJson( *(const UA_ByteString*)element );
		case UA_DATATYPEKIND_DATETIME: return UADateTime{ *(const UA_DateTime*)element }.ToJson();
		case UA_DATATYPEKIND_GUID: return Opc::ToJson( *(const UA_Guid*)element );
		case UA_DATATYPEKIND_NODEID: return Opc::ToJson( *(const UA_NodeId*)element );
		case UA_DATATYPEKIND_EXPANDEDNODEID: return Opc::ToJson( *(const UA_ExpandedNodeId*)element );
		case UA_DATATYPEKIND_QUALIFIEDNAME: return Opc::ToJson( *(const UA_QualifiedName*)element );
		case UA_DATATYPEKIND_LOCALIZEDTEXT: return jstring{ ToSV(((const UA_LocalizedText*)element)->text) };//the text alone.
		default: return VisitNumeric( type, [element]<class N>(){ return toJson( *(const N*)element ); }, [&]{ return encoded( element, type ); } );
		}
	}
	α Variant::ToJson()ε->jvalue{
		if( IsNull() )
			return {};
		if( IsScalar() )
			return ElementToJson( data, *type );
		jarray y;
		for( uint i=0; i<arrayLength; ++i )
			y.emplace_back( ElementToJson((UA_Byte*)data+i*type->memSize, *type) );
		return y;
	}
}
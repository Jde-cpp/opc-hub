#include <jde/opc/uatypes/Variant.h>
#include <charconv>
#include <absl/container/flat_hash_map.h>
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

	//What no arm here has a shape for - a DataValue, a DiagnosticInfo, a Decimal - as open62541's json encoder writes it.
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

	//A structure's members as open62541 lays them out (ua_types.c copyStructure and copyUnion):  each after its padding;  an array as its
	//uint count then its pointer, an optional one absent when the pointer is null (an empty one present is the sentinel);  an optional
	//scalar by pointer, null when absent;  an absent member goes out as null;  a union's selected member alone, at its padding from the
	//start, after the UInt32 switch field, and none selected as an empty object.  Value::Set's fillStructure walks the same layout back.
	Ω structureToJson( const void* p, const UA_DataType& type, const ITypes* types )ε->jobject{
		auto ptr = reinterpret_cast<uintptr_t>( p );
		jobject y;
		let array = [&]( const UA_DataTypeMember& m, uintptr_t at )ε->jvalue{
			let count = *reinterpret_cast<const uint*>( at );
			let data = *reinterpret_cast<const UA_Byte* const*>( at+sizeof(uint) );
			if( m.isOptional && !data )
				return jvalue{};
			jarray a;
			for( uint i=0; data && (const void*)data!=UA_EMPTY_ARRAY_SENTINEL && i<count; ++i )
				a.push_back( Variant::ElementToJson(data+i*m.memberType->memSize, *m.memberType, types) );
			return a;
		};
		if( type.typeKind==UA_DATATYPEKIND_UNION ){
			let selection = *reinterpret_cast<const UA_UInt32*>( ptr );
			if( selection && selection<=type.membersSize ){
				let& m = type.members[selection-1];
				y[m.memberName] = m.isArray ? array( m, ptr+m.padding ) : Variant::ElementToJson( reinterpret_cast<const void*>(ptr+m.padding), *m.memberType, types );
			}
			return y;
		}
		for( uint i=0; i<type.membersSize; ++i ){
			let& m = type.members[i];
			ptr += m.padding;
			if( m.isArray ){
				y[m.memberName] = array( m, ptr );
				ptr += sizeof(uint)+sizeof(void*);
			}
			else if( m.isOptional ){
				let member = *reinterpret_cast<const void* const*>( ptr );
				y[m.memberName] = member ? Variant::ElementToJson( member, *m.memberType, types ) : jvalue{};
				ptr += sizeof(void*);
			}
			else{
				y[m.memberName] = Variant::ElementToJson( reinterpret_cast<const void*>(ptr), *m.memberType, types );
				ptr += m.memberType->memSize;
			}
		}
		return y;
	}
	//Decoded, the structure it holds;  encoded with a type UA_TYPES or `types` explains, that structure, decoded against the chain for its
	//own members' types;  else the opaque form, {typeId, body}, the body base64 for a binary one, text for an xml one, absent for none -
	//and the encoding id noted with `types`, whose fetch the next read may find it by.
	Ω extensionObjectToJson( const UA_ExtensionObject& eo, const ITypes* types )ε->jvalue{
		if( eo.encoding>=UA_EXTENSIONOBJECT_DECODED )
			return eo.content.decoded.type && eo.content.decoded.data ? Variant::ElementToJson( eo.content.decoded.data, *eo.content.decoded.type, types ) : jvalue{};
		let& enc = eo.content.encoded;
		if( eo.encoding==UA_EXTENSIONOBJECT_ENCODED_BYTESTRING ){
			if( let type = FindEncoding(enc.typeId, types); type ){
				void* data = UA_new( type );
				THROW_IF( !data, "Could not allocate a '{}'.", type->typeName );
				UA_DecodeBinaryOptions options{};
				options.customTypes = types ? types->Chain() : nullptr;
				let sc = UA_decodeBinary( &enc.body, data, type, &options );
				if( !sc ){
					try{
						auto y = Variant::ElementToJson( data, *type, types );
						UA_delete( data, type );
						return y;
					}
					catch( ... ){
						UA_delete( data, type );
						throw;
					}
				}
				UA_delete( data, type );
				WARNT( ELogTags::Parsing, "Could not decode a '{}' body of {} bytes - {}.  Serializing it as it came.", type->typeName, enc.body.length, UAException::Message(sc) );
			}
			else if( enc.typeId.namespaceIndex && types )
				types->Missing( enc.typeId );
		}
		jobject y{ {"typeId", Opc::ToJson(enc.typeId)} };
		if( eo.encoding==UA_EXTENSIONOBJECT_ENCODED_BYTESTRING )
			y["body"] = ByteStringToBase64( enc.body );
		else if( eo.encoding==UA_EXTENSIONOBJECT_ENCODED_XML )
			y["body"] = jstring{ ToSV(enc.body) };
		return y;
	}

	Ω variantToJson( const UA_Variant& v, const ITypes* types )ε->jvalue{
		if( UA_Variant_isEmpty(&v) )
			return {};
		if( UA_Variant_isScalar(&v) )
			return Variant::ElementToJson( v.data, *v.type, types );
		jarray y;
		for( uint i=0; i<v.arrayLength; ++i )
			y.emplace_back( Variant::ElementToJson((const UA_Byte*)v.data+i*v.type->memSize, *v.type, types) );
		return y;
	}
	α Variant::ElementToJson( const void* element, const UA_DataType& type, const ITypes* types )ε->jvalue{
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
		case UA_DATATYPEKIND_ENUM: return jvalue{ *(const UA_Int32*)element };//its value:  what an enumeration encodes as.
		case UA_DATATYPEKIND_EXTENSIONOBJECT: return extensionObjectToJson( *(const UA_ExtensionObject*)element, types );
		case UA_DATATYPEKIND_VARIANT: return variantToJson( *(const UA_Variant*)element, types );//its value, walked as a top-level one is.
		case UA_DATATYPEKIND_STRUCTURE:
		case UA_DATATYPEKIND_OPTSTRUCT:
		case UA_DATATYPEKIND_UNION: return structureToJson( element, type, types );
		default: return VisitNumeric( type, [element]<class N>(){ return toJson( *(const N*)element ); }, [&]{ return encoded( element, type ); } );
		}
	}
	α Variant::ToJson( const ITypes* types )ε->jvalue{
		return variantToJson( *this, types );
	}

	Ω isStructure( const UA_DataType& type )ι->bool{ return type.typeKind==UA_DATATYPEKIND_STRUCTURE || type.typeKind==UA_DATATYPEKIND_OPTSTRUCT || type.typeKind==UA_DATATYPEKIND_UNION; }
	//Each element of each member of the structure `p` of `type`, laid out as structureToJson reads it, to `f( element, memberType )` until
	//it answers false;  false when it did.
	Ω allMembers( void* p, const UA_DataType& type, const auto& f )ι->bool{
		auto ptr = reinterpret_cast<uintptr_t>( p );
		let array = [&]( const UA_DataTypeMember& m, uintptr_t at )->bool{
			let count = *reinterpret_cast<const uint*>( at );
			let data = *reinterpret_cast<UA_Byte* const*>( at+sizeof(uint) );
			for( uint i=0; data && (const void*)data!=UA_EMPTY_ARRAY_SENTINEL && i<count; ++i ){
				if( !f(data+i*m.memberType->memSize, *m.memberType) )
					return false;
			}
			return true;
		};
		if( type.typeKind==UA_DATATYPEKIND_UNION ){
			let selection = *reinterpret_cast<const UA_UInt32*>( ptr );
			if( !selection || selection>type.membersSize )
				return true;
			let& m = type.members[selection-1];
			return m.isArray ? array( m, ptr+m.padding ) : f( reinterpret_cast<void*>(ptr+m.padding), *m.memberType );
		}
		for( uint i=0; i<type.membersSize; ++i ){
			let& m = type.members[i];
			ptr += m.padding;
			if( m.isArray ){
				if( !array(m, ptr) )
					return false;
				ptr += sizeof(uint)+sizeof(void*);
			}
			else if( m.isOptional ){
				if( let member = *reinterpret_cast<void* const*>( ptr ); member && !f(member, *m.memberType) )
					return false;
				ptr += sizeof(void*);
			}
			else{
				if( !f(reinterpret_cast<void*>(ptr), *m.memberType) )
					return false;
				ptr += m.memberType->memSize;
			}
		}
		return true;
	}
	Ω allElements( const UA_Variant& v, const auto& f )ι->bool{
		if( !v.type || !v.data || (const void*)v.data==UA_EMPTY_ARRAY_SENTINEL )
			return true;
		let count = UA_Variant_isScalar( &v ) ? 1 : v.arrayLength;
		for( uint i=0; i<count; ++i ){
			if( !f((UA_Byte*)v.data+i*v.type->memSize, *v.type) )
				return false;
		}
		return true;
	}
	//A kind whose value may hold another type:  a Variant, an ExtensionObject, a DataValue, a structure's members.
	Ω container( const UA_DataType& type )ι->bool{
		return type.typeKind==UA_DATATYPEKIND_VARIANT || type.typeKind==UA_DATATYPEKIND_EXTENSIONOBJECT || type.typeKind==UA_DATATYPEKIND_DATAVALUE || isStructure( type );
	}
	//The element `p` of `type` is a type outside UA_TYPES, or holds one anywhere:  a Variant of one, an ExtensionObject decoded as one,
	//or a member of namespace 0's that does - a KeyValuePair's Value, an EventFieldList's fields, a DataValue's value.
	Ω holdsCustom( const void* p, const UA_DataType& type )ι->bool{
		if( !IsBuiltIn(&type) )
			return true;
		let walk = []( void* element, const UA_DataType& t )ι->bool{ return !holdsCustom( element, t ); };
		switch( type.typeKind ){
		case UA_DATATYPEKIND_VARIANT:{
			let& v = *(const UA_Variant*)p;
			return v.type && ( !IsBuiltIn(v.type) || (container(*v.type) && !allElements(v, walk)) );}
		case UA_DATATYPEKIND_EXTENSIONOBJECT:{
			let& eo = *(const UA_ExtensionObject*)p;
			return eo.encoding>=UA_EXTENSIONOBJECT_DECODED && eo.content.decoded.type && ( !IsBuiltIn(eo.content.decoded.type) || (eo.content.decoded.data && holdsCustom(eo.content.decoded.data, *eo.content.decoded.type)) );}
		case UA_DATATYPEKIND_DATAVALUE:{
			let& dv = *(const UA_DataValue*)p;
			return dv.hasValue && holdsCustom( &dv.value, UA_TYPES[UA_TYPES_VARIANT] );}
		default:
			return isStructure( type ) && !allMembers( const_cast<void*>(p), type, walk );
		}
	}
	//The structure `p` of `type` as the ExtensionObject it travels as:  the type's DefaultBinary id and its binary form, as open62541's
	//encoder writes a structure held in a Variant.  One that won't encode is an empty ExtensionObject, never one that keeps the type.
	Ω encodeStructure( const void* p, const UA_DataType& type, UA_ExtensionObject& y )ι->void{
		UA_ExtensionObject_init( &y );
		y.encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
		StatusCode sc = UA_NodeId_copy( &type.binaryEncodingId, &y.content.encoded.typeId );
		if( !sc )
			sc = UA_encodeBinary( p, &type, &y.content.encoded.body, nullptr );
		if( sc ){
			WARNT( ELogTags::Parsing, "Could not encode a '{}' to keep it - {}.  Kept as an empty ExtensionObject.", type.typeName, UAException::Message(sc) );
			UA_ExtensionObject_clear( &y );//the id copied, and whatever body was written.
		}
	}
	//`v`, of a type outside UA_TYPES, as an ExtensionObject of each element, in v's shape.
	Ω encodeVariant( const UA_Variant& v, UA_Variant& y )ι->void{
		UA_Variant_init( &y );
		let scalar = UA_Variant_isScalar( &v );
		let count = scalar || !v.data ? (uint)scalar : v.arrayLength;
		auto objects = (UA_ExtensionObject*)UA_Array_new( count, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
		if( !objects ){
			WARNT( ELogTags::Parsing, "Could not allocate {} ExtensionObjects to keep a '{}' - kept empty.", count, v.type->typeName );
			return;
		}
		for( uint i=0; i<count; ++i )
			encodeStructure( (const UA_Byte*)v.data+i*v.type->memSize, *v.type, objects[i] );
		if( scalar )
			UA_Variant_setScalar( &y, objects, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );//one element from UA_Array_new frees as a scalar does.
		else{
			UA_Variant_setArray( &y, objects, count, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
			if( let sc = UA_Array_copy(v.arrayDimensions, v.arrayDimensionsSize, (void**)&y.arrayDimensions, &UA_TYPES[UA_TYPES_UINT32]); !sc )
				y.arrayDimensionsSize = v.arrayDimensionsSize;
		}
	}
	//In place, in a copy:  every type outside UA_TYPES under the element `p` of `type` encoded, holdsCustom's places walked.
	Ω encodeCustom( void* p, const UA_DataType& type )ι->void{
		let walk = []( void* element, const UA_DataType& t )ι->bool{ encodeCustom( element, t ); return true; };
		switch( type.typeKind ){
		case UA_DATATYPEKIND_VARIANT:{
			auto& v = *(UA_Variant*)p;
			if( !v.type )
				break;
			if( !IsBuiltIn(v.type) ){
				UA_Variant y; encodeVariant( v, y );
				UA_Variant_clear( &v );
				v = y;
			}
			else if( container(*v.type) )
				allElements( v, walk );
			break;}
		case UA_DATATYPEKIND_EXTENSIONOBJECT:{
			auto& eo = *(UA_ExtensionObject*)p;
			if( eo.encoding<UA_EXTENSIONOBJECT_DECODED || !eo.content.decoded.type || !eo.content.decoded.data )
				break;
			if( !IsBuiltIn(eo.content.decoded.type) ){
				UA_ExtensionObject y; encodeStructure( eo.content.decoded.data, *eo.content.decoded.type, y );
				UA_ExtensionObject_clear( &eo );
				eo = y;
			}
			else
				encodeCustom( eo.content.decoded.data, *eo.content.decoded.type );
			break;}
		case UA_DATATYPEKIND_DATAVALUE:{
			auto& dv = *(UA_DataValue*)p;
			if( dv.hasValue )
				encodeCustom( &dv.value, UA_TYPES[UA_TYPES_VARIANT] );
			break;}
		default:
			if( isStructure(type) )
				allMembers( p, type, walk );
		}
	}
	α Variant::WithBuiltInTypes( const UA_Variant& v )ι->optional<Variant>{
		if( !holdsCustom(&v, UA_TYPES[UA_TYPES_VARIANT]) )
			return nullopt;
		if( !IsBuiltIn(v.type) ){//the elements encoded straight from `v`:  a copy of them would only be cleared.
			UA_Variant y; encodeVariant( v, y );
			return Variant{ move(y) };
		}
		Variant y{ v };
		encodeCustom( (UA_Variant*)&y, UA_TYPES[UA_TYPES_VARIANT] );
		return y;
	}
	Ω noteMissing( const UA_Variant& v, const ITypes& types )ι->void{
		if( !v.type || !v.data || (v.type->typeKind!=UA_DATATYPEKIND_EXTENSIONOBJECT && v.type->typeKind!=UA_DATATYPEKIND_VARIANT) )
			return;
		let count = UA_Variant_isScalar( &v ) ? 1 : v.arrayLength;
		for( uint i=0; i<count; ++i ){
			if( v.type->typeKind==UA_DATATYPEKIND_VARIANT ){
				noteMissing( ((const UA_Variant*)v.data)[i], types );
				continue;
			}
			let& eo = ((const UA_ExtensionObject*)v.data)[i];
			if( eo.encoding==UA_EXTENSIONOBJECT_ENCODED_BYTESTRING && eo.content.encoded.typeId.namespaceIndex && !FindEncoding(eo.content.encoded.typeId, &types) )
				types.Missing( eo.content.encoded.typeId );
		}
	}
}

namespace Jde{
	α Opc::FindEncoding( const UA_NodeId& encodingId, const ITypes* types )ι->const UA_DataType*{
		static const absl::flat_hash_map<UA_UInt32,const UA_DataType*> ns0 = []{//UA_TYPES by DefaultBinary id, every one of which is numeric in namespace 0.
			absl::flat_hash_map<UA_UInt32,const UA_DataType*> y;
			for( uint i=0; i<UA_TYPES_COUNT; ++i ){
				if( let& encoding = UA_TYPES[i].binaryEncodingId; !encoding.namespaceIndex && encoding.identifierType==UA_NODEIDTYPE_NUMERIC && encoding.identifier.numeric )
					y.emplace( encoding.identifier.numeric, &UA_TYPES[i] );
			}
			return y;
		}();
		if( !encodingId.namespaceIndex && encodingId.identifierType==UA_NODEIDTYPE_NUMERIC ){
			if( let p = ns0.find(encodingId.identifier.numeric); p!=ns0.end() )
				return p->second;
		}
		for( auto a = types ? types->Chain() : nullptr; a; a = a->next ){
			for( uint i=0; i<a->typesSize; ++i ){
				if( UA_NodeId_equal(&a->types[i].binaryEncodingId, &encodingId) )
					return &a->types[i];
			}
		}
		return nullptr;
	}
	α Opc::FindDataType( const UA_NodeId& typeId, const ITypes* types )ι->const UA_DataType*{
		return UA_findDataTypeWithCustom( &typeId, types ? types->Chain() : nullptr );
	}
	//An ExtensionObject element, or a Variant element's, at any depth.
	α Opc::NoteMissingTypes( const UA_Variant& v, const ITypes& types )ι->void{
		noteMissing( v, types );
	}
}
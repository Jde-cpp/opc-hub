#include <jde/opc/proto/opc.Common.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/Variant.h>

#define let const auto

namespace Jde::Opc{
	//ToBinaryString's inverse.  A guid shorter than 16 bytes leaves the tail zeroed.
	Ω toGuid( sv bytes )ι->UA_Guid{
		UA_Guid y{};
		::memcpy( &y, bytes.data(), std::min(sizeof(UA_Guid), bytes.size()) );
		return y;
	}

	α ProtoUtils::ToExNodeId( const Proto::ExpandedNodeId& proto )ι->ExNodeId{
		ExNodeId y;
		y.namespaceUri = AllocUAString( proto.namespace_uri() );
		y.serverIndex = (uint32)proto.server_index();
		y.nodeId = ToNodeId( proto.node() ).Move(); //Move(), or the owning temporary frees the identifier at the end of this expression.
		return y;
	}
	α ProtoUtils::ToExNodeId( const UA_ExpandedNodeId& id )ι->Proto::ExpandedNodeId{
		Proto::ExpandedNodeId y;
		*y.mutable_node() = ToNodeId( id.nodeId );
		if( id.namespaceUri.length )
			y.set_namespace_uri( ToSV(id.namespaceUri) );
		y.set_server_index( id.serverIndex );
		return y;
	}
	α ProtoUtils::ToNodeId( const UA_NodeId& id )ι->Proto::NodeId{
		Proto::NodeId y;
		y.set_namespace_index( id.namespaceIndex );
		switch( id.identifierType ){
			case UA_NodeIdType::UA_NODEIDTYPE_NUMERIC:
				y.set_numeric( id.identifier.numeric );
				break;
			case UA_NodeIdType::UA_NODEIDTYPE_STRING:
				y.set_string( ToSV(id.identifier.string) );
				break;
			case UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING:
				y.set_byte_string( ToSV(id.identifier.byteString) );
				break;
			case UA_NodeIdType::UA_NODEIDTYPE_GUID:
				y.set_guid( ToBinaryString(id.identifier.guid) );
				break;
		}
		return y;
	}
	α ProtoUtils::ToNodeId( const Proto::NodeId& id )ι->NodeId{
		NodeId y;
		y.namespaceIndex = ( int16 )id.namespace_index();
		if( id.has_numeric() ){
			y.identifierType = UA_NodeIdType::UA_NODEIDTYPE_NUMERIC;
			y.identifier.numeric = id.numeric();
		}
		else if( id.has_string() ){
			y.identifierType = UA_NodeIdType::UA_NODEIDTYPE_STRING;
			y.identifier.string = AllocUAString( id.string() );
		}
		else if( id.has_byte_string() ){
			y.identifierType = UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING;
			y.identifier.byteString = AllocUAString( id.byte_string() );
		}
		else if( id.has_guid() ){
			y.identifierType = UA_NodeIdType::UA_NODEIDTYPE_GUID;
			y.identifier.guid = toGuid( id.guid() );
		}
		return y;
	}
	α ProtoUtils::ToNodeIds( google::protobuf::RepeatedPtrField<Proto::NodeId>&& proto )ι->flat_set<NodeId>{
		flat_set<NodeId> nodes;
		for( auto& p : proto )
			nodes.insert( ToNodeId(move(p)) );
		return nodes;
	}

	//A UA_Duration is a double count of milliseconds, not a time point - see #20.  protobuf wants seconds and nanos to
	//share a sign (a Timestamp is the opposite, nanos non-negative), so truncate toward zero.  Clamped to protobuf's own
	//documented Duration range first:  a NaN or 1e300 would make the integer casts undefined.
	constexpr double _maxDurationMs = 315'576'000'000.*1000.;//±10000 years, what a google.protobuf.Duration is defined for.
	Ω toDurationProto( UA_Duration ms )ι->google::protobuf::Duration{
		let clamped = std::isfinite( ms ) ? std::clamp( ms, -_maxDurationMs, _maxDurationMs ) : 0.;
		let seconds = std::trunc( clamped/1000. );
		google::protobuf::Duration y;
		y.set_seconds( (int64_t)seconds );
		y.set_nanos( (int32_t)((clamped-seconds*1000.)*1'000'000.) );
		return y;
	}
	Ω toDuration( const google::protobuf::Duration& d )ι->UA_Duration{
		return d.seconds()*1000. + d.nanos()/1'000'000.;//the same arithmetic Value::Set's {seconds,nanos} branch does.
	}

	//Straight from the ticks, not through UADateTime:  that goes via a TimePoint, which libc++ keeps in microseconds, so
	//the last of UA_DateTime's 100ns digits would not survive.  UA_DATETIME_UNIX_EPOCH is whole seconds, so splitting
	//before shifting cannot overflow, and every UA_DateTime - INT64_MIN and INT64_MAX included - comes back exactly.
	constexpr UA_Int64 _epochSeconds = UA_DATETIME_UNIX_EPOCH/UA_DATETIME_SEC;
	Ω toTimestamp( UA_DateTime dt )ι->google::protobuf::Timestamp{
		auto seconds = dt/UA_DATETIME_SEC-_epochSeconds;
		auto ticks = dt%UA_DATETIME_SEC;
		if( ticks<0 ){//a Timestamp's nanos are never negative.
			ticks += UA_DATETIME_SEC;
			--seconds;
		}
		google::protobuf::Timestamp y;
		y.set_seconds( seconds );
		y.set_nanos( (int32_t)(ticks*100) );
		return y;
	}
	//Saturating, as UADateTime's Timestamp ctor is:  a Timestamp can hold seconds no UA_DateTime can.
	Ω toDateTime( const google::protobuf::Timestamp& t )ι->UA_DateTime{
		using Limits = std::numeric_limits<UA_DateTime>;
		constexpr UA_Int64 maxSeconds = (Limits::max)()/UA_DATETIME_SEC-_epochSeconds;
		constexpr UA_Int64 minSeconds = (Limits::min)()/UA_DATETIME_SEC-_epochSeconds;
		let s = t.seconds();
		if( s>maxSeconds )
			return (Limits::max)();
		if( s<minSeconds )
			return (Limits::min)();
		let base = (s+_epochSeconds)*UA_DATETIME_SEC;
		let ticks = std::clamp<UA_Int64>( t.nanos(), 0, 999'999'999 )/100;
		return ticks>(Limits::max)()-base ? (Limits::max)() : base+ticks;
	}

	//Part 6 5.1.2's built-in type id, the only type a Variant carries on the wire:  an enum travels as an Int32, and
	//Decimal and the structures as ExtensionObjects.
	Ω builtInType( const UA_DataType& type )ε->uint32{
		let kind = type.typeKind;
		if( kind<=UA_DATATYPEKIND_DIAGNOSTICINFO )
			return kind+1;
		if( kind==UA_DATATYPEKIND_ENUM )
			return UA_DATATYPEKIND_INT32+1;
		if( kind==UA_DATATYPEKIND_BITFIELDCLUSTER )
			throw UAException{ UA_STATUSCODE_BADNOTSUPPORTED, Ƒ("'{}' is not a value type.", type.typeName) };
		return UA_DATATYPEKIND_EXTENSIONOBJECT+1;
	}

	//The body open62541's encoder writes for a structure in a Variant: the type's DefaultBinary id and its binary form.
	Ω encoded( const void* p, const UA_DataType& type )ε->Proto::ExtensionObject{
		Proto::ExtensionObject y;
		*y.mutable_type_id() = ProtoUtils::ToNodeId( type.binaryEncodingId );
		UAString body;
		if( let sc = UA_encodeBinary(p, &type, &body, nullptr); sc )
			throw UAException{ sc, Ƒ("Could not encode a '{}'.", type.typeName) };
		y.set_binary( ToSV(body) );
		return y;
	}
	Ω toExtensionObject( const UA_ExtensionObject& eo )ε->Proto::ExtensionObject{
		if( eo.encoding>=UA_EXTENSIONOBJECT_DECODED ){
			let& decoded = eo.content.decoded;
			if( !decoded.type || !decoded.data )
				throw UAException{ UA_STATUSCODE_BADENCODINGERROR, "A decoded ExtensionObject has no value." };
			return encoded( decoded.data, *decoded.type );
		}
		Proto::ExtensionObject y;
		*y.mutable_type_id() = ProtoUtils::ToNodeId( eo.content.encoded.typeId );
		if( eo.encoding==UA_EXTENSIONOBJECT_ENCODED_BYTESTRING )
			y.set_binary( ToSV(eo.content.encoded.body) );
		else if( eo.encoding==UA_EXTENSIONOBJECT_ENCODED_XML )
			y.set_xml( ToSV(eo.content.encoded.body) );
		return y;
	}

	#define CASE(kind, T, setter) case UA_DATATYPEKIND_##kind: y.set_##setter( *(const T*)p ); break;
	#define STR_CASE(kind, setter) case UA_DATATYPEKIND_##kind: y.set_##setter( ToSV(*(const UA_String*)p) ); break;
	α ProtoUtils::ToValue( const void* p, const UA_DataType& type )ε->Proto::Value{
		Proto::Value y;
		switch( type.typeKind ){
		CASE( BOOLEAN, UA_Boolean, boolean )
		CASE( SBYTE, UA_SByte, sbyte )
		CASE( BYTE, UA_Byte, byte )
		CASE( INT16, UA_Int16, int16 )
		CASE( UINT16, UA_UInt16, uint16 )
		case UA_DATATYPEKIND_ENUM:
		CASE( INT32, UA_Int32, int32 )
		CASE( UINT32, UA_UInt32, uint32 )
		CASE( INT64, UA_Int64, int64 )
		CASE( UINT64, UA_UInt64, uint64 )
		CASE( FLOAT, UA_Float, float_value )
		CASE( STATUSCODE, UA_StatusCode, status_code )
		STR_CASE( STRING, string_value )
		STR_CASE( BYTESTRING, byte_string )
		STR_CASE( XMLELEMENT, xml_element )
		case UA_DATATYPEKIND_DOUBLE:
			if( &type==&UA_TYPES[UA_TYPES_DURATION] )
				*y.mutable_duration() = toDurationProto( *(const UA_Duration*)p );
			else
				y.set_double_value( *(const UA_Double*)p );
			break;
		case UA_DATATYPEKIND_DATETIME:
			*y.mutable_date() = toTimestamp( *(const UA_DateTime*)p );
			break;
		case UA_DATATYPEKIND_GUID:
			y.set_guid( ToBinaryString(*(const UA_Guid*)p) );
			break;
		case UA_DATATYPEKIND_NODEID:
			*y.mutable_node() = ToNodeId( *(const UA_NodeId*)p );
			break;
		case UA_DATATYPEKIND_EXPANDEDNODEID:
			*y.mutable_expanded_node() = ToExNodeId( *(const UA_ExpandedNodeId*)p );
			break;
		case UA_DATATYPEKIND_QUALIFIEDNAME:{
			let& qn = *(const UA_QualifiedName*)p;
			auto& proto = *y.mutable_qualified_name();
			proto.set_namespace_index( qn.namespaceIndex );
			proto.set_name( ToSV(qn.name) );
			break;}
		case UA_DATATYPEKIND_LOCALIZEDTEXT:{
			let& lt = *(const UA_LocalizedText*)p;
			auto& proto = *y.mutable_localized_text();
			if( lt.locale.data )
				proto.set_locale( ToSV(lt.locale) );
			if( lt.text.data )
				proto.set_text( ToSV(lt.text) );
			break;}
		case UA_DATATYPEKIND_EXTENSIONOBJECT:
			*y.mutable_extension_object() = toExtensionObject( *(const UA_ExtensionObject*)p );
			break;
		case UA_DATATYPEKIND_VARIANT://an element of a Variant array.
			y = ToValue( *(const UA_Variant*)p );
			break;
		case UA_DATATYPEKIND_DECIMAL:
		case UA_DATATYPEKIND_STRUCTURE:
		case UA_DATATYPEKIND_OPTSTRUCT:
		case UA_DATATYPEKIND_UNION:
			*y.mutable_extension_object() = encoded( p, type );
			break;
		default://DataValue, DiagnosticInfo, BitfieldCluster:  what Supported refuses.
			throw UAException{ UA_STATUSCODE_BADNOTSUPPORTED, Ƒ("A '{}' value is not supported.", type.typeName) };
		}
		return y;
	}
	α ProtoUtils::Supported( const UA_Variant& v )ι->bool{
		if( !v.type )
			return true;
		switch( v.type->typeKind ){
		case UA_DATATYPEKIND_DATAVALUE:
		case UA_DATATYPEKIND_DIAGNOSTICINFO:
		case UA_DATATYPEKIND_BITFIELDCLUSTER:
			return false;
		case UA_DATATYPEKIND_VARIANT:{
			let elements = std::span{ (const UA_Variant*)v.data, UA_Variant_isScalar(&v) ? 1 : v.arrayLength };
			return std::ranges::all_of( elements, []( const UA_Variant& e ){ return Supported(e); } );}
		default:
			return true;
		}
	}
	#undef CASE
	#undef STR_CASE

	α ProtoUtils::ToValue( const UA_Variant& v )ε->Proto::Value{
		if( !v.type )
			return {};
		if( UA_Variant_isScalar(&v) )
			return ToValue( v.data, *v.type );
		Proto::Value y;
		auto& a = *y.mutable_array();
		a.set_type( builtInType(*v.type) );
		a.set_is_null( !v.data );//a length -1 array, which a server may send - not UA_EMPTY_ARRAY_SENTINEL.
		for( uint i=0; i<v.arrayLength; ++i )
			*a.add_values() = ToValue( (const UA_Byte*)v.data+i*v.type->memSize, *v.type );
		for( uint i=0; i<v.arrayDimensionsSize; ++i )
			a.add_dimensions( v.arrayDimensions[i] );
		return y;
	}

	Ω scalar( const void* p, const UA_DataType& type )ε->Variant{
		UA_Variant y{};
		if( let sc = UA_Variant_setScalarCopy(&y, p, &type); sc )
			throw UAException{ sc, Ƒ("Could not set a '{}' value.", type.typeName) };
		return Variant{ move(y) };
	}
	template<class T, class... Args> Ω scalar( const UA_DataType& type, Args&&... args )ε->Variant{
		const T v{ FWD(args)... };
		return scalar( &v, type );
	}

	//Each element is decoded on its own, then moved into its slot, as Value::SetArray does.  The elements pick the
	//descriptor, so a Duration array comes back as one, and have to agree with each other and with `type`.
	Ω toArray( const Proto::Array& a )ε->Variant{
		if( a.is_null() && a.values_size() )
			throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("A null array holds {} values.", a.values_size()) };
		const UA_DataType* type{};
		if( let id = a.type(); id ){
			if( id>UA_DATATYPEKIND_DIAGNOSTICINFO+1 )
				throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("{} is not a built-in type.", id) };
			type = &UA_TYPES[id-1];//the built-in types lead UA_TYPES, in id order.
		}
		vector<Variant> elements; elements.reserve( a.values_size() );
		for( let& v : a.values() )
			elements.push_back( ProtoUtils::ToVariant(v) );
		if( !type )
			type = elements.empty() || !elements.front().type ? &UA_TYPES[UA_TYPES_VARIANT] : elements.front().type;
		let variants = type->typeKind==UA_DATATYPEKIND_VARIANT;
		if( !variants && elements.size() ){
			let first = elements.front().type;
			for( let& e : elements ){
				if( !e.type )//first may be null too, so name the declared type.
					throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("A '{}' array holds an empty value.", type->typeName) };
				if( e.type!=first )
					throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("A '{}' array holds a '{}'.", first->typeName, e.type->typeName) };
				if( !e.IsScalar() )
					throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("A '{}' array holds a nested array.", first->typeName) };
			}
			if( builtInType(*first)!=builtInType(*type) )
				throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("A '{}' array holds '{}' values.", type->typeName, first->typeName) };
			type = first;
		}
		uint length = 1;
		for( let d : a.dimensions() )
			length *= d;
		if( a.dimensions_size() && length!=elements.size() )
			throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("Dimensions of {} elements for {} values.", length, elements.size()) };

		auto data = a.is_null() ? nullptr : UA_Array_new( elements.size(), type );//null encodes as length -1, the sentinel as 0.
		if( !data && !a.is_null() )
			throw UAException{ UA_STATUSCODE_BADOUTOFMEMORY };
		for( uint i=0; i<elements.size(); ++i ){
			auto slot = (UA_Byte*)data+i*type->memSize;
			auto& e = elements[i];
			if( variants ){
				let raw = e.Move();
				::memcpy( slot, &raw, sizeof(UA_Variant) );
			}
			else{
				::memcpy( slot, e.data, type->memSize );
				UA_free( e.data );//the box, not what it points at - that moved into the slot.
				UA_Variant_init( &e );
			}
		}
		UA_Variant y{};
		UA_Variant_setArray( &y, data, elements.size(), type );
		if( a.dimensions_size() ){
			y.arrayDimensions = (UA_UInt32*)UA_Array_new( a.dimensions_size(), &UA_TYPES[UA_TYPES_UINT32] );
			y.arrayDimensionsSize = a.dimensions_size();
			std::ranges::copy( a.dimensions(), y.arrayDimensions );
		}
		return Variant{ move(y) };
	}

	α ProtoUtils::ToVariant( const Proto::Value& v )ε->Variant{
		using enum Proto::Value::OfCase;
		switch( v.of_case() ){
		case kBoolean: return scalar<UA_Boolean>( UA_TYPES[UA_TYPES_BOOLEAN], v.boolean() );
		case kByte: return scalar<UA_Byte>( UA_TYPES[UA_TYPES_BYTE], (UA_Byte)v.byte() );
		case kByteString: return scalar<UA_ByteString>( UA_TYPES[UA_TYPES_BYTESTRING], ToUV(v.byte_string()) );
		case kDate: return scalar<UA_DateTime>( UA_TYPES[UA_TYPES_DATETIME], toDateTime(v.date()) );
		case kDoubleValue: return scalar<UA_Double>( UA_TYPES[UA_TYPES_DOUBLE], v.double_value() );
		case kDuration: return scalar<UA_Duration>( UA_TYPES[UA_TYPES_DURATION], toDuration(v.duration()) );
		case kExpandedNode:{
			let x = ToExNodeId( v.expanded_node() );
			return scalar( static_cast<const UA_ExpandedNodeId*>(&x), UA_TYPES[UA_TYPES_EXPANDEDNODEID] );}//upcast, not &x: the wrapper is a `const void*` away from handing the vendor whatever sits at its offset 0.
		case kFloatValue: return scalar<UA_Float>( UA_TYPES[UA_TYPES_FLOAT], v.float_value() );
		case kGuid:{
			let x = toGuid( v.guid() );
			return scalar( &x, UA_TYPES[UA_TYPES_GUID] );}
		case kInt16: return scalar<UA_Int16>( UA_TYPES[UA_TYPES_INT16], (UA_Int16)v.int16() );
		case kInt32: return scalar<UA_Int32>( UA_TYPES[UA_TYPES_INT32], v.int32() );
		case kInt64: return scalar<UA_Int64>( UA_TYPES[UA_TYPES_INT64], v.int64() );
		case kNode:{
			let x = ToNodeId( v.node() );
			return scalar( static_cast<const UA_NodeId*>(&x), UA_TYPES[UA_TYPES_NODEID] );}//upcast, not &x: NodeId is polymorphic, so &x is the vptr.
		case kSbyte: return scalar<UA_SByte>( UA_TYPES[UA_TYPES_SBYTE], (UA_SByte)v.sbyte() );
		case kStatusCode: return scalar<UA_StatusCode>( UA_TYPES[UA_TYPES_STATUSCODE], v.status_code() );
		case kStringValue: return scalar<UA_String>( UA_TYPES[UA_TYPES_STRING], ToUV(v.string_value()) );
		case kUint16: return scalar<UA_UInt16>( UA_TYPES[UA_TYPES_UINT16], (UA_UInt16)v.uint16() );
		case kUint32: return scalar<UA_UInt32>( UA_TYPES[UA_TYPES_UINT32], v.uint32() );
		case kUint64: return scalar<UA_UInt64>( UA_TYPES[UA_TYPES_UINT64], v.uint64() );
		case kXmlElement: return scalar<UA_XmlElement>( UA_TYPES[UA_TYPES_XMLELEMENT], ToUV(v.xml_element()) );
		case kLocalizedText:{
			let& lt = v.localized_text();
			return scalar<UA_LocalizedText>( UA_TYPES[UA_TYPES_LOCALIZEDTEXT], lt.has_locale() ? ToUV(lt.locale()) : UA_STRING_NULL, lt.has_text() ? ToUV(lt.text()) : UA_STRING_NULL );}
		case kQualifiedName: return scalar<UA_QualifiedName>( UA_TYPES[UA_TYPES_QUALIFIEDNAME], (UA_UInt16)v.qualified_name().namespace_index(), ToUV(v.qualified_name().name()) );
		case kExtensionObject:{
			let& proto = v.extension_object();
			let typeId = ToNodeId( proto.type_id() );
			UA_ExtensionObject x{};//ENCODED_NOBODY
			x.content.encoded.typeId = typeId;//borrowed - scalar() copies it.
			if( proto.has_binary() ){
				x.encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
				x.content.encoded.body = ToUV( proto.binary() );
			}
			else if( proto.has_xml() ){
				x.encoding = UA_EXTENSIONOBJECT_ENCODED_XML;
				x.content.encoded.body = ToUV( proto.xml() );
			}
			return scalar( &x, UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );}
		case kArray: return toArray( v.array() );
		case OF_NOT_SET: return Variant{ UA_Variant{} };
		}
		throw UAException{ UA_STATUSCODE_BADDECODINGERROR, Ƒ("Unknown Value member {}.", (int)v.of_case()) };
	}
}

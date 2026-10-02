#pragma once
#include <span>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/str.h>
#include "../usings.h"
#include "UAString.h"

#define let const auto
namespace Jde::Opc{
	struct NodeId;
	α FindDataType( const NodeId& nodeId )ι->UA_DataType*;
	constexpr α operator ""_uv( const char* x, uint len )ι->UA_String{ return UA_String{ len, static_cast<UA_Byte*>((void*)x) }; } //(UA_Byte*) gcc error
	Ξ IsKind( const UA_DataType* type, uint16 uaTypeIndex )ι->bool{ return type && type->typeKind==UA_TYPES[uaTypeIndex].typeKind; }
	Ξ ToJson( UA_UInt64 v )ι->jobject{ return jobject{ {"high", v>>32}, {"low", v&0xFFFFFFFF}, {"unsigned",true} }; };
	Ξ ToJson( UA_Int64 v )ι->jobject{ return jobject{ {"high", v>>32}, {"low", v&0xFFFFFFFF}, {"unsigned",false} }; };
	Ξ ToGuid( const UA_Guid& ua )ι->uuid{ //data1/2/3 are native-endian in UA_Guid, big-endian (RFC 4122) in uuid.
		uuid y;
		y.data[0]=(uint8_t)(ua.data1>>24); y.data[1]=(uint8_t)(ua.data1>>16); y.data[2]=(uint8_t)(ua.data1>>8); y.data[3]=(uint8_t)ua.data1;
		y.data[4]=(uint8_t)(ua.data2>>8); y.data[5]=(uint8_t)ua.data2;
		y.data[6]=(uint8_t)(ua.data3>>8); y.data[7]=(uint8_t)ua.data3;
		::memcpy( y.data+8, ua.data4, sizeof(ua.data4) );
		return y;
	}
	Ξ ToUAGuid( const uuid& id )ι->UA_Guid{
		UA_Guid ua;
		ua.data1 = (UA_UInt32)id.data[0]<<24 | (UA_UInt32)id.data[1]<<16 | (UA_UInt32)id.data[2]<<8 | id.data[3];
		ua.data2 = (UA_UInt16)( id.data[4]<<8 | id.data[5] );
		ua.data3 = (UA_UInt16)( id.data[6]<<8 | id.data[7] );
		::memcpy( ua.data4, id.data+8, sizeof(ua.data4) );
		return ua;
	}
	Ξ ToJson( UA_Guid v )ι->jstring{ return jstring{ Jde::ToString(ToGuid(v)) }; }
	Ξ ByteStringToJson( const UA_ByteString& v )ι->jstring{ return jstring{ Str::ToHex(std::span{v.data, v.length}) }; }
	Ξ ByteStringToBase64( const UA_ByteString& v )ι->jstring{
		if( !v.length )
			return jstring{};
		UAString y; //empty - UA_ByteString_toBase64 allocates.
		return UA_ByteString_toBase64( &v, &y ) ? jstring{} : jstring{ ToSV(y) };
	}
	Ξ ToGuid( sv x, UA_Guid& ua )ε->void{ ua = ToUAGuid( Jde::ToUuid(x) ); }
	Ξ ToBinaryString( const UA_Guid& ua )ι->string{ return {(const char*)&ua, sizeof(UA_Guid)}; }
	using ByteStringPtr = up<UA_ByteString,decltype(&UA_ByteString_delete)>;
	Ŧ ToUAByteString( const T& bytes )ι->ByteStringPtr{
		ByteStringPtr y{ UA_ByteString_new(), UA_ByteString_delete };
		if( bytes.size() ){
			UA_ByteString_allocBuffer( y.get(), bytes.size() );
			memcpy( y->data, bytes.data(), bytes.size() );
		}
		return y;
	};

	Ξ FromByteString( const UA_ByteString& bytes )ι->vector<uint8_t>{
		vector<uint8_t> y;
		if( bytes.length ){
			y.resize( bytes.length );
			memcpy( y.data(), bytes.data, bytes.length );
		}
		return y;
	};

	//absl::Hash over the fields NodeId/ExNodeId's operator< compares, so it agrees with their equality (reviews/abseil.md D3).
	Ŧ HashNodeId( T h, const UA_NodeId& n )ι->T{
		switch( n.identifierType ){
			case UA_NODEIDTYPE_NUMERIC: return T::combine( std::move(h), n.namespaceIndex, n.identifierType, n.identifier.numeric );
			case UA_NODEIDTYPE_STRING: return T::combine( std::move(h), n.namespaceIndex, n.identifierType, ToSV(n.identifier.string) );
			case UA_NODEIDTYPE_BYTESTRING: return T::combine( std::move(h), n.namespaceIndex, n.identifierType, ToSV(n.identifier.byteString) );
			default: return T::combine_contiguous( T::combine(std::move(h), n.namespaceIndex, n.identifierType), (const unsigned char*)&n.identifier.guid, sizeof(UA_Guid) );
		}
	}

	Τ using Iterable = std::span<T>;
}
#undef let
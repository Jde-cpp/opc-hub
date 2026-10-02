#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/db/Row.h>
#include <jde/db/Value.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/UAException.h>

#define let const auto
namespace Jde::Opc{
	ExNodeId::ExNodeId ( const ExNodeId& x )ι:
		UA_ExpandedNodeId{UA_EXPANDEDNODEID_NULL}{
		nodeId = x.Copy();
		if( x.namespaceUri.length )
			UA_String_copy( &x.namespaceUri, &namespaceUri );
		serverIndex = x.serverIndex;
	}

	ExNodeId::ExNodeId( const NodeId& x )ι:
		ExNodeId{}{
		UA_NodeId_copy( static_cast<const UA_NodeId*>(&x), &nodeId );
	}

	ExNodeId::ExNodeId( const flat_map<string,string>& x )ε:
		UA_ExpandedNodeId{UA_EXPANDEDNODEID_NULL}{
		if( auto p = x.find("serverindex"); p!=x.end() )
			serverIndex = To<UA_UInt32>( p->second );
		if( auto p = x.find("ns"); p!=x.end() )
			nodeId.namespaceIndex = Str::TryTo<UA_UInt16>( p->second ).value_or( 0 );
		if( auto p = x.find("s"); p!=x.end() ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_STRING;
			nodeId.identifier.string = AllocUAString( p->second );
		}
		else if( auto p = x.find("i"); p!=x.end() ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_NUMERIC;
			nodeId.identifier.numeric = To<UA_UInt32>( p->second );
		}
		else if( auto p = x.find("b"); p!=x.end() ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING;
			auto t = ToUV( p->second );
			if( let sc = UA_ByteString_fromBase64(&nodeId.identifier.byteString, &t); sc ){
				UA_ByteString_clear( &nodeId.identifier.byteString );//the ctor never completes, so ~ExNodeId will not run to free it.
				throw UAException{ sc, Ƒ("Could not base64-decode byte-string node id '{}'.", p->second) };
			}
		}
		else if( auto p = x.find("g"); p!=x.end() ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_GUID;
			ToGuid( p->second, nodeId.identifier.guid );
		}
		else
			DBGT( ELogTags::Parsing, "No identifier in nodeId" );
		if( auto p = x.find("nsu"); p!=x.end() ) //allocated last: a throw above must not follow an allocation or the dtor never runs to free it.
			namespaceUri = AllocUAString( p->second );
	}

	//The jvalue overloads of Json::FindDefaultSV/FindNumber take a json *pointer path*, not a member name, so "nsu" never
	//resolved and both extra fields were dropped on every construction from json.  Read them off the object, where the
	//overloads do take a key.  if_object rather than as_object: a bare `5002`/`"tag"` is a legal node id here, which
	//NodeId::FromJson accepts and which as_object would throw on.
	ExNodeId::ExNodeId( const jvalue& j )ε:
		UA_ExpandedNodeId{ NodeId::FromJson(j), UA_STRING_NULL, 0 }{
		if( let* o = j.if_object(); o ){
			serverIndex = Json::FindNumber<UA_UInt32>( *o, "serverindex" ).value_or( 0 );
			if( let nsu = Json::FindSV(*o, "nsu"); nsu && nsu->size() ) //allocated last, as in the rest-params ctor above.
				namespaceUri = AllocUAString( *nsu );
		}
	}

	ExNodeId::ExNodeId( ExNodeId&& x )ι:
		UA_ExpandedNodeId{UA_EXPANDEDNODEID_NULL}{
		nodeId = x.Move();
		namespaceUri = x.namespaceUri;
		serverIndex = x.serverIndex;
		UA_ExpandedNodeId_init( &x );
	}

	ExNodeId::ExNodeId( DB::Row& r, uint8 index, bool extended )ε:
		UA_ExpandedNodeId{UA_EXPANDEDNODEID_NULL}{
		string uri; UA_UInt32 server{};
		if( extended ){//these 2 could throw.
			uri = r.GetString( index+5 );
			server = r.GetOpt<uint32_t>( index+6 ).value_or( 0 );
		}
		nodeId.namespaceIndex = r.Get<uint16>( index );
		if( !r.IsNull(index+1) ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_NUMERIC;
			nodeId.identifier.numeric = r.Get<UA_UInt32>( index+1 );
		}
		else if( !r.IsNull(index+2) ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_STRING;
			nodeId.identifier.string = AllocUAString( r.GetString(index+2) );
		}
		else if( !r.IsNull(index+3) ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_GUID;
			nodeId.identifier.guid = ToUAGuid( r.GetGuid(index+3) );
		}
		else if( !r.IsNull(index+4) ){
			nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING;
			auto bytes = r.GetBytes( index+4 );
			UA_ByteString_allocBuffer( &nodeId.identifier.byteString, bytes.size() );
			::memcpy( nodeId.identifier.byteString.data, bytes.data(), bytes.size() );
		}
		namespaceUri = extended ? AllocUAString( uri ) : UA_STRING_NULL;
		serverIndex = server;
	}

	α ExNodeId::InsertParams( bool extended )Ι->vector<DB::Value>{
		vector<DB::Value> params; params.reserve( extended ? 7 : 5 );
		using enum UA_NodeIdType;
		params.emplace_back( nodeId.namespaceIndex );
		params.emplace_back( IsNumeric() ? DB::Value{*Numeric()} : DB::Value{} );
		params.emplace_back( IsString() ? DB::Value{*String()} : DB::Value{} );
		params.emplace_back( IsGuid() ? DB::Value{*Guid()} : DB::Value{} );
		params.emplace_back( IsBytes() ? DB::Value{FromByteString(*Bytes())} : DB::Value{} );
		if( extended ){
			params.emplace_back( namespaceUri.length ? DB::Value{ToString(namespaceUri)} : DB::Value{} );
			params.emplace_back( serverIndex );
		}
		return params;
	}

	α ExNodeId::operator=( ExNodeId&& x )ι->ExNodeId&{
		if( this!=&x ){
			Clear();
			nodeId = x.Move();
			namespaceUri=x.namespaceUri;
			serverIndex=x.serverIndex;
			UA_ExpandedNodeId_init( &x );
		}
		return *this;
	}

	α ExNodeId::Clear()ι->void{
		UA_ExpandedNodeId_clear( static_cast<UA_ExpandedNodeId*>(this) );
	}

	α ExNodeId::operator=( const ExNodeId& x )ι->ExNodeId&{
		if( this==&x )
			return *this;
		Clear();
		nodeId = x.Copy();
		if( x.namespaceUri.length )
			UA_String_copy( &x.namespaceUri, &namespaceUri );
		serverIndex = x.serverIndex;
		return *this;
	}
	α ExNodeId::operator<( const ExNodeId& x )Ι->bool{
		return
			ToSV(namespaceUri)==ToSV(x.namespaceUri) ?
				serverIndex==x.serverIndex ?
					nodeId.namespaceIndex==x.nodeId.namespaceIndex ?
						nodeId.identifierType==x.nodeId.identifierType ?
							nodeId.identifierType==UA_NodeIdType::UA_NODEIDTYPE_NUMERIC ? nodeId.identifier.numeric<x.nodeId.identifier.numeric :
								nodeId.identifierType==UA_NodeIdType::UA_NODEIDTYPE_STRING ? ToSV(nodeId.identifier.string)<ToSV(x.nodeId.identifier.string) :
								nodeId.identifierType==UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING ? ToSV(nodeId.identifier.byteString)<ToSV(x.nodeId.identifier.byteString)
							: memcmp( &nodeId.identifier.guid, &x.nodeId.identifier.guid, sizeof(UA_Guid) )<0
						: nodeId.identifierType<x.nodeId.identifierType
					: nodeId.namespaceIndex<x.nodeId.namespaceIndex
				: serverIndex<x.serverIndex
			: ToSV(namespaceUri)<ToSV(x.namespaceUri);
	}

	α ExNodeId::Copy()Ι->UA_NodeId{
		UA_NodeId y{};
		UA_NodeId_copy( &nodeId, &y );
		return y;
	}

	α ExNodeId::Move()ι->UA_NodeId{
		UA_NodeId y{};
    y.namespaceIndex = nodeId.namespaceIndex;
    y.identifierType = nodeId.identifierType;
		if( nodeId.identifierType==UA_NodeIdType::UA_NODEIDTYPE_NUMERIC )
			y.identifier.numeric = nodeId.identifier.numeric;
		else if( nodeId.identifierType==UA_NodeIdType::UA_NODEIDTYPE_STRING )
	    y.identifier.string = nodeId.identifier.string;
		else if( nodeId.identifierType==UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING )
			y.identifier.byteString = nodeId.identifier.byteString;
		else if( nodeId.identifierType==UA_NodeIdType::UA_NODEIDTYPE_GUID )
			y.identifier.guid = nodeId.identifier.guid;
		memset( &nodeId, 0, sizeof(UA_NodeId) );
		return y;
	}

	α ExNodeId::ToJson()Ι->jobject{
		return Opc::ToJson( *this );
	}

	α ExNodeId::to_string()Ι->string{
		return serialize( ToJson() );
	}
	α ExNodeId::Add( jobject& j )Ι->void{
		if( namespaceUri.length )
			j["nsu"] = ToSV(namespaceUri);
		if( serverIndex )
			j["serverindex"] = serverIndex;
		AddNodeId( j, nodeId, true );//true: an ExpandedNodeId omits ns 0, the way it omits an empty nsu and server 0.
	}
}
namespace Jde{
	α Opc::ToJson( const UA_ExpandedNodeId& x )ε->jobject{
		jobject j;
		if( x.namespaceUri.length )
			j["nsu"] = ToSV(x.namespaceUri);
		if( x.serverIndex )
			j["serverindex"] = x.serverIndex;
		AddNodeId( j, x.nodeId, true );
		return j;
	}
}

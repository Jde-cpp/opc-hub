#include "NamespacesAwait.h"
#include <jde/opc/uatypes/ExNodeId.h>
#include "../UAClient.h"
#include "../types/UAClientException.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α Namespaces::Index( sv uri )Ι->optional<NsIndex>{
		let p = std::ranges::find( Uris, uri );
		return p==Uris.end() ? nullopt : optional<NsIndex>{ (NsIndex)(p-Uris.begin()) };
	}
	α Namespaces::ToNodeId( const ExNodeId& node )Ι->optional<NodeId>{
		let index = node.namespaceUri.length ? Index( ToSV(node.namespaceUri) ) : optional<NsIndex>{ node.NsIndex() };
		if( !index )
			return nullopt;
		NodeId y{ node.nodeId };
		y.namespaceIndex = *index;
		return y;
	}
	α Namespaces::ToExNodeId( const UA_NodeId& node )Ι->optional<ExNodeId>{
		let uri = Uri( node.namespaceIndex );
		if( !uri )
			return nullopt;
		ExNodeId y{ NodeId{node} };
		y.nodeId.namespaceIndex = 0;
		y.namespaceUri = AllocUAString( *uri );
		return y;
	}

	α NamespacesAwait::Execute()ι->TAwait<flat_map<NodeId,Value>>::Task{
		try{
			auto values = co_await ReadValueAwait{ {NodeId{(UA_UInt16)0, (UA_UInt32)UA_NS0ID_SERVER_NAMESPACEARRAY}}, _client, _sl };
			THROW_IF( values.empty(), "No result reading the namespace array." );
			let& value = values.begin()->second;
			if( value.hasStatus && value.status )
				throw UAClientException{ (StatusCode)value.status, _client->Handle(), "read(Server_NamespaceArray)" };
			let& variant = value.value;
			THROW_IF( variant.type!=&UA_TYPES[UA_TYPES_STRING] || UA_Variant_isScalar(&variant), "The namespace array is a '{}'{}, not a string array.", variant.type ? variant.type->typeName : "null", UA_Variant_isScalar(&variant) ? " scalar" : "" );
			Namespaces y; y.Uris.reserve( variant.arrayLength );
			for( uint i=0; i<variant.arrayLength; ++i )
				y.Uris.push_back( ToString(((UA_String*)variant.data)[i]) );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
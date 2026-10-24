#include <jde/opc/uatypes/opcHelpers.h>
#include <open62541/server.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/Value.h>

namespace Jde{
	α Opc::NamespaceIndex( UA_Server& server, sv uri, SL sl )ε->NsIndex{
		size_t index{};
		if( const auto sc = UA_Server_getNamespaceByName( &server, ToUV(uri), &index ); sc )
			throw UAException{ sc, Ƒ("namespace '{}' is not loaded", uri), {}, sl };
		return (NsIndex)index;
	}
	α Opc::ReadValue( UA_Server& server, const UA_NodeId& node, UA_TimestampsToReturn timestamps )ι->Value{
		UA_ReadValueId id; UA_ReadValueId_init( &id );
		id.nodeId = node;
		id.attributeId = UA_ATTRIBUTEID_VALUE;
		return Value{ UA_Server_read(&server, &id, timestamps) };
	}
}
#pragma once
#include <open62541/server.h>
#include <jde/opc/uatypes/NodeId.h>

//A historizing variable's HA Configuration object (Part 11 §5.2.2), added where its address space has none:  the
//OpcServer's for what its nodesets leave out, the gateway's for its served nodes.
namespace Jde::Opc::Server::HAConfiguration{
	constexpr sv BrowseName{ "HA Configuration" };
	//Where what this adds goes:  open62541's namespace for the server itself, its applicationUri, which every server has from
	//creation (ua_server.c).  A host's own namespaces - a nodeset's, a served connection's - are added from 2.
	constexpr UA_UInt16 OwnNamespace{ 1 };
	α Child( UA_Server& ua, const UA_NodeId& parent, sv browseName )ι->optional<NodeId>;//by a standard browse name:  namespace 0.
	//Writes the property, adding it in OwnNamespace when there is none.
	α Publish( UA_Server& ua, const UA_NodeId& parent, sv browseName, const UA_DataType& type, const void* scalar )ε->NodeId;
	α Add( UA_Server& ua, const UA_NodeId& variable )ε->NodeId;//the object, without properties.
}
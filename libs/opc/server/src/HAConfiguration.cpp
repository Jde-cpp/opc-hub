#include <jde/opc/server/HAConfiguration.h>
#include <jde/opc/uatypes/opcHelpers.h>

#define let const auto
namespace Jde::Opc::Server{
	//A standard browse name:  namespace 0.
	Ω name( sv text )ι->UA_QualifiedName{ return { 0, {text.size(), (UA_Byte*)text.data()} }; }

	α HAConfiguration::Child( UA_Server& ua, const UA_NodeId& parent, sv browseName )ι->optional<NodeId>{
		let qualified = name( browseName );
		auto found = UA_Server_browseSimplifiedBrowsePath( &ua, parent, 1, &qualified );
		optional<NodeId> y;
		if( found.statusCode==UA_STATUSCODE_GOOD && found.targetsSize )
			y.emplace( found.targets[0].targetId.nodeId );
		UA_BrowsePathResult_clear( &found );
		return y;
	}
	α HAConfiguration::Publish( UA_Server& ua, const UA_NodeId& parent, sv browseName, const UA_DataType& type, const void* scalar )ε->NodeId{
		UA_Variant value; UA_Variant_setScalar( &value, const_cast<void*>(scalar), &type );
		if( auto existing = Child(ua, parent, browseName) ){
			UAε( UA_Server_writeValue(&ua, *existing, value) );
			return move( *existing );
		}
		UA_VariableAttributes attributes = UA_VariableAttributes_default;
		attributes.displayName = UA_LocalizedText{ {}, {browseName.size(), (UA_Byte*)browseName.data()} };
		attributes.dataType = type.typeId;
		attributes.valueRank = UA_VALUERANK_SCALAR;
		attributes.value = value;
		UA_NodeId id{};
		UAε( UA_Server_addVariableNode(&ua, UA_NODEID_NUMERIC(1, 0), parent, UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY), name(browseName), UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), attributes, nullptr, &id) );
		return NodeId{ move(id) };
	}
	α HAConfiguration::Add( UA_Server& ua, const UA_NodeId& variable )ε->NodeId{
		UA_ObjectAttributes attributes = UA_ObjectAttributes_default;
		attributes.displayName = UA_LocalizedText{ {}, {BrowseName.size(), (UA_Byte*)BrowseName.data()} };
		UA_NodeId id{};
		UAε( UA_Server_addObjectNode(&ua, UA_NODEID_NUMERIC(1, 0), variable, UA_NODEID_NUMERIC(0, UA_NS0ID_HASHISTORICALCONFIGURATION), name(BrowseName), UA_NODEID_NUMERIC(0, UA_NS0ID_HISTORICALDATACONFIGURATIONTYPE), attributes, nullptr, &id) );
		return NodeId{ move(id) };
	}
}
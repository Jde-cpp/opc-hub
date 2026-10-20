#pragma once
#include "MonitoringParameters.h"

namespace Jde::Opc::Gateway{

	struct CreateMonitoredItemsRequest : Owned<UA_CreateMonitoredItemsRequest, UA_TYPES_CREATEMONITOREDITEMSREQUEST>{
		//One item per node, in the map's order - the server answers by position, which is how UAMonitoringNodes reads the
		//response.  `timestamps` is the request's:  BOTH by default - init is SOURCE alone, and NodeValues carries the server's too.
		CreateMonitoredItemsRequest( const flat_map<NodeId,MonitoringParameters>& nodes, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ι;
		α ToJson()ι->jarray;
	};

	inline CreateMonitoredItemsRequest::CreateMonitoredItemsRequest( const flat_map<NodeId,MonitoringParameters>& nodes, UA_TimestampsToReturn timestamps )ι{
		timestampsToReturn = timestamps;
		itemsToCreateSize = nodes.size();
		itemsToCreate = (UA_MonitoredItemCreateRequest*)UA_Array_new( itemsToCreateSize, &UA_TYPES[UA_TYPES_MONITOREDITEMCREATEREQUEST] );
		uint i=0;
		for( auto&& [n,parameters] : nodes ){//auto&&:  flat_map's iterator hands back a proxy pair.
			auto& item = itemsToCreate[i++];
			UA_NodeId_copy( &n, &item.itemToMonitor.nodeId );//deep copy - clear() owns it; assignment would alias the NodeId's heap data.
			item.itemToMonitor.attributeId = UA_ATTRIBUTEID_VALUE;
			item.monitoringMode = UA_MONITORINGMODE_REPORTING;
			item.requestedParameters.samplingInterval = parameters.SamplingInterval;
			item.requestedParameters.queueSize = parameters.QueueSize;
			item.requestedParameters.discardOldest = parameters.DiscardOldest;
		}
	}
	Ξ CreateMonitoredItemsRequest::ToJson()ι->jarray{
		jarray values;
		for( auto request : Iterable<UA_MonitoredItemCreateRequest>(itemsToCreate, itemsToCreateSize) ){
			jobject j;
			j["nodeId"] = Opc::ToJson( request.itemToMonitor.nodeId );
			j["attributeId"] = request.itemToMonitor.attributeId==13 ? "VALUE" : std::to_string(request.itemToMonitor.attributeId);
			j["monitoringMode"] = request.monitoringMode;
			j["samplingInterval"] = request.requestedParameters.samplingInterval;
			j["queueSize"] = request.requestedParameters.queueSize;
			j["discardOldest"] = request.requestedParameters.discardOldest;
			values.push_back( j );
		}
		return values;
	}
}
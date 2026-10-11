#pragma once
#include "MonitoringParameters.h"

namespace Jde::Opc::Gateway{

	struct CreateMonitoredItemsRequest : UA_CreateMonitoredItemsRequest{
		//One item per node, in the map's order - the server answers by position, which is how UAMonitoringNodes reads the
		//response.  `timestamps` is the request's:  BOTH by default - init is SOURCE alone, and NodeValues carries the server's too.
		CreateMonitoredItemsRequest( const flat_map<NodeId,MonitoringParameters>& nodes, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ι;
		CreateMonitoredItemsRequest( flat_set<NodeId>&& nodes, const MonitoringParameters& parameters={}, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ι;//every node alike
		CreateMonitoredItemsRequest( CreateMonitoredItemsRequest&& x )ι;
		~CreateMonitoredItemsRequest(){ UA_CreateMonitoredItemsRequest_clear( this ); }
		α operator=( CreateMonitoredItemsRequest&& x )ι->CreateMonitoredItemsRequest&;
		α ToJson()ι->jarray;
	};

	inline CreateMonitoredItemsRequest::CreateMonitoredItemsRequest( const flat_map<NodeId,MonitoringParameters>& nodes, UA_TimestampsToReturn timestamps )ι{
		UA_CreateMonitoredItemsRequest_init( this );
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
	inline CreateMonitoredItemsRequest::CreateMonitoredItemsRequest( flat_set<NodeId>&& nodes, const MonitoringParameters& parameters, UA_TimestampsToReturn timestamps )ι:
		CreateMonitoredItemsRequest{ [&]{ flat_map<NodeId,MonitoringParameters> y; for( auto& n : nodes ) y.emplace( n, parameters ); return y; }(), timestamps }
	{}
	inline CreateMonitoredItemsRequest::CreateMonitoredItemsRequest( CreateMonitoredItemsRequest&& x )ι:
		UA_CreateMonitoredItemsRequest{ x }{
		UA_CreateMonitoredItemsRequest_init( &x );
	}

	Ξ CreateMonitoredItemsRequest::operator=( CreateMonitoredItemsRequest&& x )ι->CreateMonitoredItemsRequest&{
		if( this!=&x ){
			UA_CreateMonitoredItemsRequest_clear( this );//free our current contents before overwriting.
			*(UA_CreateMonitoredItemsRequest*)this = x;//shallow-transfer ownership from x...
			UA_CreateMonitoredItemsRequest_init( &x );//...then zero x so its dtor won't free what we now own.
		}
		return *this;
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
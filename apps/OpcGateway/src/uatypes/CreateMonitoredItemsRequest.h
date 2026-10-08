#pragma once

namespace Jde::Opc::Gateway{

	struct CreateMonitoredItemsRequest : UA_CreateMonitoredItemsRequest{
		CreateMonitoredItemsRequest( flat_set<NodeId>&& nodes )ι;
		CreateMonitoredItemsRequest( CreateMonitoredItemsRequest&& x )ι;
		~CreateMonitoredItemsRequest(){ UA_CreateMonitoredItemsRequest_clear( this ); }
		α operator=( CreateMonitoredItemsRequest&& x )ι->CreateMonitoredItemsRequest&;
		α ToJson()ι->jarray;
	};

	inline CreateMonitoredItemsRequest::CreateMonitoredItemsRequest( flat_set<NodeId>&& nodes )ι{
		UA_CreateMonitoredItemsRequest_init( this );
		timestampsToReturn = UA_TIMESTAMPSTORETURN_BOTH;//init is SOURCE alone, and NodeValues carries the server's too
		itemsToCreateSize = nodes.size();
		itemsToCreate = (UA_MonitoredItemCreateRequest*)UA_Array_new( itemsToCreateSize, &UA_TYPES[UA_TYPES_MONITOREDITEMCREATEREQUEST] );
		uint i=0;
		for( auto& n : nodes ){
			auto& item = itemsToCreate[i++];
			UA_NodeId_copy( &n, &item.itemToMonitor.nodeId );//deep copy - clear() owns it; assignment would alias the NodeId's heap data.
			item.itemToMonitor.attributeId = UA_ATTRIBUTEID_VALUE;
			item.monitoringMode = UA_MONITORINGMODE_REPORTING;
			item.requestedParameters.samplingInterval = 500.0;
			item.requestedParameters.queueSize = 1;
			item.requestedParameters.discardOldest = true;
		}
	}
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
			//j["samplingInterval"] = item.requestedParameters.samplingInterval;
			//j["itemsToCreate"][i]["requestedParameters"]["queueSize"] = item.requestedParameters.queueSize;
			//j["itemsToCreate"][i]["requestedParameters"]["discardOldest"] = item.requestedParameters.discardOldest;
			values.push_back( j );
		}
		return values;
	}
}
#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include "../uatypes/MonitoringParameters.h"

namespace Jde::Opc::Gateway{
	struct CreateMonitoredItemsRequest;
	struct UAClient;

	struct DeleteMonitoredItemsAwait : VoidAwait{
		DeleteMonitoredItemsAwait( flat_map<SubscriptionId,flat_set<MonitorId>> monitoredItems, sp<UAClient> client, SRCE )ι;
		α await_ready()ι->bool override{ return !_client; }
		α Suspend()ι->void override;
		α OnComplete( UA_DeleteMonitoredItemsResponse& response, RequestId requestId )ι->void;
	private:
		α SuspendOnStrand()ι->void;//the UA submissions, dispatched onto the client's strand by Suspend.
		α TryResume()ι->void;//resume once all successfully-submitted deletes have completed.
		sp<UAClient> _client;
		flat_map<SubscriptionId,flat_set<MonitorId>> _monitoredItems;
		std::mutex _mutex;
		flat_set<RequestId> _finished;
		uint _submitted{};//count of async deletes actually accepted (not _monitoredItems.size(): a mid-loop submit failure means fewer callbacks will fire).
		bool _submissionsComplete{};
		bool _resumed{};
		constexpr static EOpcLogTags _tags{ EOpcLogTags::Monitoring };
	};

	struct MonitoredItemModifyResult final{ StatusCode Status{}; double RevisedSamplingInterval{}; uint32 RevisedQueueSize{}; };
	//ModifyMonitoredItems:  new sampling, queue and discard settings for items this client already monitors, changed in place, so
	//a node whose MinTimeInterval was edited keeps sampling through the change (spec *Collector contract*) - a delete and create
	//would not.  One request per subscription the items are on;  the response's revised values go back to UAMonitoringNodes.  A
	//node this client has no item for answers BadMonitoredItemIdInvalid without a round trip, and a request refused outright fails
	//each of its nodes with the refusal, so every node asked about is in the result.
	struct ModifyMonitoredItemsAwait final : TAwait<flat_map<NodeId,MonitoredItemModifyResult>>{
		using base = TAwait<flat_map<NodeId,MonitoredItemModifyResult>>;
		ModifyMonitoredItemsAwait( flat_map<NodeId,MonitoringParameters> nodes, sp<UAClient> client, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH, SRCE )ι;
		α Suspend()ι->void override;
		α OnComplete( UA_ModifyMonitoredItemsResponse& response, RequestId requestId )ι->void;
	private:
		α SuspendOnStrand()ι->void;//the UA submissions, on the client's strand - as DeleteMonitoredItemsAwait.
		α TryResume()ι->void;//once every accepted submission has answered.
		flat_map<NodeId,MonitoringParameters> _nodes;
		sp<UAClient> _client;
		UA_TimestampsToReturn _timestamps;
		flat_map<RequestId,vector<NodeId>> _requests;//each request's nodes in its items' order - its results answer them by position.  Strand-confined.
		flat_map<NodeId,MonitoredItemModifyResult> _results;//strand-confined until the resume takes it.
		std::mutex _mutex;
		uint _submitted{}, _finished{};
		bool _submissionsComplete{};
		bool _resumed{};
		constexpr static EOpcLogTags _tags{ EOpcLogTags::Monitoring };
	};
}
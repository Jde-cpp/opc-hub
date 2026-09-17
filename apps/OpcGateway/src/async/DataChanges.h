#pragma once
#include "../uatypes/MonitoredItemCreateResult.h"

namespace Jde::Opc::Gateway{
	struct IDataChange; struct UAClient;
	struct DataChangeAwait final : TAwait<FromServer::SubscriptionAck>{
		using base = TAwait<FromServer::SubscriptionAck>;
		DataChangeAwait( flat_set<NodeId> nodes, sp<IDataChange> dataChange, sp<UAClient> c, SRCE )ι:base{sl}, _nodes{move(nodes)}, _dataChange{move(dataChange)}, _client{move(c)}{}
		α Suspend()ι->void override;
		α await_resume()ι->FromServer::SubscriptionAck override;
		α OnComplete( UA_CreateMonitoredItemsResponse* response )ι->void;
	private:
		α Submit()ι->void;//on the strand
		α Redrive()ι->VoidTask;//build a subscription, then Submit again - see Submit.
		flat_set<NodeId> _nodes;
		sp<IDataChange> _dataChange;
		sp<UAClient> _client;
		RequestId _requestId{};
		Jde::Handle _monitoredRequestId{};//0 until MonitoredItemsRequest registers - and still 0 if it never could.
		bool _redriven{};
		flat_map<NodeId, MonitoredItemCreateResult> _existingNodes;
	};
}
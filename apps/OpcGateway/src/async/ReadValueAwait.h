#pragma once

namespace Jde::Opc{ struct Value; }
namespace Jde::Opc::Gateway {
	struct UAClient;
	struct ReadValueAwait final : TAwait<flat_map<NodeId, Value>>{
		using base = TAwait<flat_map<NodeId, Value>>;
		ReadValueAwait( flat_set<NodeId> x, sp<UAClient> c, SRCE )ι;
		ReadValueAwait( flat_set<NodeId> x, sp<UAClient> c, UA_TimestampsToReturn timestamps, SRCE )ι;//the first asks for neither timestamp, as open62541's own value read does
		α await_ready()ι->bool override{ return _nodes.size()==0; }
		α Suspend()ι->void override;
		α OnComplete( RequestId requestId, StatusCode sc, UA_DataValue* val )ι->void;
	private:
		flat_set<NodeId> _nodes;
		sp<UAClient> _client;
		UA_TimestampsToReturn _timestamps;
		flat_map<RequestId, NodeId> _requests;//strand-confined (Suspend closure + OnComplete)
		flat_map<NodeId, Value> _results;//strand-confined
	};
}
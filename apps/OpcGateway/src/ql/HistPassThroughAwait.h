#pragma once
#include <jde/ql/types/TableQL.h>
#include "HistQL.h"
#include "../async/HistoryReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//What the raw read (HistQLAwait) and the at-time/processed one (HistComputedQLAwait) share:  a server's own history,
	//read for the caller over the caller's session (spec *Pass-through*), one HistoryRead a round for the nodes holding the
	//merge back, and every continuation point still held released before it answers.  Execute is that frame - a processed
	//read's aggregate resolved, the rounds, the release, the answer or the failure - and the subclass supplies its rounds'
	//requests, what it takes from each node's page, when to stop, and the page.
	struct HistPassThroughAwait : TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>{
		using base = TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>;
		HistPassThroughAwait( QL::TableQL&& query, sp<UAClient> client, SL sl )ι:base{ sl }, _client{ move(client) }, _query{ move(query) }{}
		α Execute()ι->TAwait<HistoryReadResponse>::Task override final;
	protected:
		struct NodeState{
			string Point;//the continuation point its last page returned.
			bool More{};//the server has more:  Point is held.
			StatusCode Status{ UA_STATUSCODE_GOOD };//the server's last answer for it.
		};
		β Begin()ι->void = 0;//once the args and the aggregate are in:  the nodes, by Args::Nodes' slot.
		β Request( bool first, bool release )ι->HistoryReadRequest = 0;//a round's;  `first` as Next said.
		β Take( uint slot, UA_HistoryReadResult& result )ι->void = 0;//a node's page.
		β Next()ι->optional<bool> = 0;//after a round:  nullopt to answer, else whether the next request is a first one.
		β Answer()ι->std::pair<vector<HistQL::ReadValue>,string> = 0;//the page and its continuation.
		β NodeCount()Ι->uint = 0;
		β State( uint slot )ι->NodeState& = 0;
		//A page's status, continuation point and More into the node's state - the status only when `status`.  False for a bad
		//answer, logged:  the node's, as the server gave it.
		α TakeHeader( uint slot, UA_HistoryReadResult& result, bool status=true )ι->bool;

		sp<UAClient> _client;
		QL::TableQL _query;
		optional<HistQL::Args> _args;
		optional<NodeId> _aggregate;//a processed read's function:  Part 13's by its standard NodeId, any other by the server's AggregateFunctions folder.
		vector<uint> _asked;//the slots the round's request names, in its order.
	private:
		α AnyMore()ι->bool;
	};
}
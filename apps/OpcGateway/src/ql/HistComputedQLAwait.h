#pragma once
#include <jde/ql/types/TableQL.h>
#include "HistQL.h"
#include "../async/HistoryReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//history with `times` or `aggregate`, and `opc`:  a server's own history at times or aggregated, Part 11's ReadAtTime
	//and ReadProcessed, read for the caller over the caller's session (spec *Pass-through*).  Each answers one value per
	//node for each requested point, a time or an interval, in the request's order, so the merge is by place, not time:
	//a call sends one HistoryRead a round, for the nodes holding the merge back, follows each node's continuation point
	//within the call, and answers the points every node has reached, a point's values in the nodes' order, up to `limit`.
	//It releases every point still held before it answers, as the raw read does.  The continuation is the next point,
	//by its place among them, and which of its nodes were answered, as a group's is:  the next page is a new HistoryRead
	//from there, for at most `limit` of the times or intervals left (Stop).  An aggregate Part 13 names goes by its
	//standard NodeId, and any other by the one the server's AggregateFunctions folder lists under that name
	//(AggregateFunctions).
	struct HistComputedQLAwait final : TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>{
		using base = TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>;
		HistComputedQLAwait( QL::TableQL&& query, sp<UAClient> client, SRCE )ι:base{ sl }, _client{ move(client) }, _query{ move(query) }{}
		α Execute()ι->TAwait<HistoryReadResponse>::Task override;
	private:
		struct Node final{
			string Point;//the continuation point its last page returned.
			bool More{};//the server has more:  Point is held.
			bool Had{};//its value at the call's first point came on a page before.
			bool Skip{};//its next value is the interval before the call's first (Back).
			vector<HistQL::ReadValue> Values;//received this call, by point from the call's first.
			StatusCode Status{ UA_STATUSCODE_GOOD };//the server's last answer for it.
		};
		//Where the merge stopped short:  the point, by its place among them all, and which of its nodes were answered.
		struct Cut final{ uint64_t Point; vector<bool> Done; };
		struct Walked final{ uint Values{}; optional<Cut> At; };
		//The request for a round:  every node on the first, then those the server has more for that hold the merge back,
		//with their points, and for a release every node still holding one.
		α Request( bool first, bool release )ι->HistoryReadRequest;
		α Take( uint slot, UA_HistoryReadResult& result )ι->void;//a node's page into its Values.
		//The merge, from the call's first point:  each point's values in the nodes' order, but those a page before answered,
		//until the page holds `limit`, a node the server has more for hasn't reached the point, or the request's points are
		//done.  Counts what it passes, moving it into `page` when given.
		α Walk( vector<HistQL::ReadValue>* page )ι->Walked;
		α Continuation( const optional<Cut>& cut )Ι->string;//empty on the last page.
		α First()Ι->uint64_t{ return _args->Continuation ? _args->Continuation->next() : 0; }//the call's first point.
		//The point after the request's last:  at most `limit` from the call's first, since Part 11's at-time and processed
		//details have no numValuesPerNode, and every point the server answers is one value a node.
		α Stop()Ι->uint64_t;
		//Whether the call asks from the interval before its first:  an aggregate read resuming at its last interval, when
		//uneven, which the server would take alone as one even interval, not Part 13's Partial uneven last one.
		α Back()Ι->bool;

		sp<UAClient> _client;
		QL::TableQL _query;
		optional<HistQL::Args> _args;
		optional<NodeId> _aggregate;//an aggregate read's function.
		vector<Node> _nodes;//by Args::Nodes' slot.
		vector<uint> _asked;//the slots the round's request names, in its order.
	};
}
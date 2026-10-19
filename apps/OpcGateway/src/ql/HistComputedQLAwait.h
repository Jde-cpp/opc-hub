#pragma once
#include "HistPassThroughAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//history with `times` or `aggregate`, and `opc`:  a server's own history at times or aggregated, Part 11's ReadAtTime
	//and ReadProcessed, read for the caller over the caller's session (spec *Pass-through*).  Each answers one value per
	//node for each requested point, a time or an interval, in the request's order, so the merge is by place, not time:
	//a call sends one HistoryRead a round, for the nodes holding the merge back, follows each node's continuation point
	//within the call, and answers the points every node has reached, a point's values in the nodes' order, up to `limit`.
	//It releases every point still held before it answers, as the raw read does.  The continuation is the next point,
	//by its place among them, and which of its nodes were answered, as a group's is:  the next page is a new HistoryRead
	//from there, for the times or intervals left that fill `limit` across the nodes, and further points of the nodes the
	//server answered when those it refused leave the page short (Extend).  An aggregate Part 13 names goes by
	//its standard NodeId, and any other by the one the server's AggregateFunctions folder lists under that name
	//(AggregateFunctions).
	struct HistComputedQLAwait final : HistPassThroughAwait{
		HistComputedQLAwait( QL::TableQL&& query, sp<UAClient> client, SRCE )ι:HistPassThroughAwait{ move(query), move(client), sl }{}
	private:
		struct Node final : NodeState{
			bool Had{};//its value at the call's first point came on a page before.
			bool Skip{};//its next value is the interval before the round's first (Back).
			vector<HistQL::ReadValue> Values;//received this call, by point from the call's first.
		};
		//Where the merge stopped short:  the point, by its place among them all, and which of its nodes were answered.
		struct Cut final{ uint64_t Point; vector<bool> Done; };
		struct Walked final{ uint Values{}; optional<Cut> At; };
		//The request for a round, of the points [_from, _stop):  a fresh one, the first or one Extend makes, names the nodes
		//Caught, then those the server has more for that hold the merge back, with their points, and for a release every
		//node still holding one.
		α Begin()ι->void override;//the nodes, and the first round's points.
		α Request( bool fresh, bool release )ι->HistoryReadRequest override;
		α Take( uint slot, UA_HistoryReadResult& result )ι->void override;//a node's page into its Values.
		//Until the page holds `limit` or the request's points are done;  a fresh round, Extend's, once the server has answered
		//every point asked and a node it refused leaves the page short.
		α Next()ι->optional<bool> override;
		α Answer()ι->std::pair<vector<HistQL::ReadValue>,string> override;
		α NodeCount()Ι->uint override{ return _nodes.size(); }
		α State( uint slot )ι->NodeState& override{ return _nodes[slot]; }
		//The merge, from the call's first point:  each point's values in the nodes' order, but those a page before answered,
		//until the page holds `limit`, a node the server has more for hasn't reached the point, or the request's points are
		//done.  Counts what it passes, moving it into `page` when given.
		α Walk( vector<HistQL::ReadValue>* page )ι->Walked;
		α Continuation( const optional<Cut>& cut )Ι->string;//empty on the last page.
		α First()Ι->uint64_t{ return _args->Continuation ? _args->Continuation->next() : 0; }//the call's first point.
		//A fresh round's points, after the last round's:  `missing` values over the nodes Caught, rounded up, since Part 11's
		//at-time and processed details have no numValuesPerNode, and every point the server answers is one value a node.
		//The first round's fill `limit` from the call's first, where every node is Caught, counting the `had` values its Had
		//nodes took there on the page before, which this page leaves out.  A later one fills the page once the server has
		//answered every point asked and a node it refused leaves the page short.  False when no node is Caught.
		α Extend( uint missing, uint had=0 )ι->bool;
		//Whether the node answered every point before _from, which a fresh request asks it from there.
		α Caught( const Node& node )Ι->bool{ return node.Values.size()==_from-First() && !UA_StatusCode_isBad( node.Status ); }
		//Whether the round asks from the interval before its first:  one of an aggregate read starting at its last interval,
		//when uneven, as a page resuming there or Extend reaching it does, which the server would take alone as one even
		//interval, not Part 13's Partial uneven last one.  A fresh request skips that value of each node it names.
		α Back()Ι->bool;

		vector<Node> _nodes;//by Args::Nodes' slot.
		uint64_t _from{}, _stop{};//the points the round's request asks, by their places among them all.
	};
}
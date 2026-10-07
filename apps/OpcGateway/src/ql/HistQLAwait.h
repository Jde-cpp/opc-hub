#pragma once
#include <jde/ql/types/TableQL.h>
#include "HistQL.h"
#include "../async/HistoryReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//hist with `opc`:  a server's own history, read for the caller over the caller's session (spec *Pass-through*).  The
	//server pages each node on its own, so a call sends one HistoryRead a round, for the nodes holding the merge back,
	//follows each node's continuation point within the call, and merges the nodes' pages by source time up to the
	//horizon, the earliest last-returned time among the nodes the server has more for, latest in a reverse read.  It
	//answers at most `limit` values up to the horizon, and releases every point still held before it does:  a point
	//kept between pages would be lost with the session, or leaked by a caller that stops.  The continuation it returns
	//is by time, as a group read's:  the next page is a new HistoryRead from there, which passes over the records at
	//that time the pages before returned, and drops the bound the server returns for the resume time.
	struct HistQLAwait final : TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>{
		using base = TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>;
		HistQLAwait( QL::TableQL&& query, sp<UAClient> client, SRCE )ι:base{ sl }, _client{ move(client) }, _query{ move(query) }{}
		α Execute()ι->TAwait<HistoryReadResponse>::Task override;
	private:
		//A value received and not yet answered:  an opening bound goes out on the first page whatever the limit, and a
		//closing bound follows the last page's values.
		struct Held final{ HistQL::ReadValue Value; bool Opening{}; bool Closing{}; };
		struct Node final{
			string Point;//the continuation point its last page returned.
			bool More{};//the server has more:  Point is held.
			optional<UA_DateTime> Last;//the time of the last value it returned this call.
			bool Opened{};//its first value this call has been seen:  the opening bound and the resume are dealt with.
			uint Skip{};//records at the resume time a page before returned, still to pass over.
			vector<Held> Pending;
			StatusCode Status{ UA_STATUSCODE_GOOD };//the server's last answer for it.
		};
		//Where the merge stands:  no node has more (Open), a node with more returned nothing yet, so nothing is final
		//(Blocked), or the time up to which values are final.
		struct Horizon final{ bool Open{ true }; bool Blocked{}; optional<UA_DateTime> Time; };
		α Reach()Ι->Horizon;
		//The request for a round:  every node on the first, then those the server has more for that hold the merge back,
		//with their points, and for a release every node still holding one.
		α Request( bool first, bool release )ι->HistoryReadRequest;
		α Take( uint slot, UA_HistoryReadResult& result )ι->void;//a node's page into its Pending.
		α Emittable()Ι->uint;//how many pending values a page could answer now.
		α Page()ι->vector<HistQL::ReadValue>;//the merge, up to the limit and the horizon, taken out of Pending.
		α Continuation( const vector<HistQL::ReadValue>& page )Ι->string;//empty on the last page.
		α ResumeAt()Ι->optional<UA_DateTime>{ return _args->Continuation ? optional<UA_DateTime>{ _args->Continuation->time() } : nullopt; }
		α Before( UA_DateTime a, UA_DateTime b )Ι->bool{ return _args->Reverse() ? a>b : a<b; }//a comes before b in the read's direction.
		α Earlier( const Held& a, uint slotA, const Held& b, uint slotB )Ι->bool;//the merge's order:  time, values before closing bounds, then the request's order.
		Ω Time( const UA_DataValue& v )ι->UA_DateTime{ return v.hasSourceTimestamp ? v.sourceTimestamp : v.serverTimestamp; }
		Ω NotFound( const UA_DataValue& v )ι->bool{ return v.hasStatus && v.status==UA_STATUSCODE_BADBOUNDNOTFOUND; }
		//The first tick after 1601:  the end a resumed read from an end alone goes back to, standing in for none.
		static constexpr UA_DateTime FirstTick{ 1 };

		sp<UAClient> _client;
		QL::TableQL _query;
		optional<HistQL::Args> _args;
		vector<Node> _nodes;//by Args::Nodes' slot.
		vector<uint> _asked;//the slots the round's request names, in its order.
		bool _instant{};//the round reads one instant, which comes forward whichever way the read flows.
	};
}
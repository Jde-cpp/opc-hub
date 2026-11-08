#pragma once
#include <jde/ql/types/TableQL.h>
#include "../async/HistoryReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//historyEvents( opc, node, start, end, select, limit, continuation ){ status continuation events{ fields } } (spec *Events*):  a
	//server's own event history of one node, read for the caller over the caller's session, as `history` reads its values (spec
	//*Pass-through*) - Part 11's ReadEventDetails from start to end, through the same async service, with an EventFilter of the
	//eight base fields then the browse paths `select` names, as a SubscribeEvents asks them.  Each event answers as its field
	//list in that order, a field the server left empty null.  At most `limit` events a call, readLimit by default;  a
	//continuation point the server returns is released before the answer, since the session would lose it, and `continuation`
	//is the last event's Time, which the next call passes back to read on from, inclusive - a caller drops the events at that
	//time it holds by their EventId.  `status` is the node's HistoryReadResult's:  Bad_HistoryOperationUnsupported from a server
	//that keeps no event history, the embedded OpcServer among them.
	struct HistEventsQLAwait final : TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>{
		using base = TAwaitEx<jvalue,TAwait<HistoryReadResponse>::Task>;
		HistEventsQLAwait( QL::TableQL&& query, sp<UAClient> client, SRCE )ι:base{ sl }, _query{ move(query) }, _client{ move(client) }{}
		static constexpr sv Command{ "historyEvents" };
		α Execute()ι->TAwait<HistoryReadResponse>::Task override;
	private:
		struct Args final{
			Args( const QL::Input& input, SL sl )ε;
			NodeId Node;
			UA_DateTime Start{}, End{};
			EventFilter Filter;
			uint32 Limit{};
		};
		α Read( const Args& args, const UA_HistoryReadResult& result )ι->jarray;//the events as field lists, Time's tick noted.
		QL::TableQL _query;
		sp<UAClient> _client;
		optional<UA_DateTime> _lastTime;//the last event's Time, the continuation.
	};
}
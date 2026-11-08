#pragma once
#include <jde/ql/types/TableQL.h>
#include "../async/HistoryReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//Where a page of historyEvents ends, as `continuation` spells it:  "<the last event's Time tick>/<events at that tick returned so far>".
	//A read on starts at the tick, inclusive, and passes over that many, so a run of events on one tick can span pages.
	struct EventsCursor final{
		Ω Parse( sv text )ι->optional<EventsCursor>;
		α Start()Ι->UA_DateTime{ return _start; }
		α Skipping()Ι->bool{ return _skip>0; }//events a page before returned are still to pass over.
		α Passes( UA_DateTime time )ι->bool;//an event at `time`:  true to pass it over, otherwise it counts as returned.
		α Next()Ι->optional<string>;//the next page's text, none when no event had a Time.
	private:
		UA_DateTime _start{};
		uint _skip{}, _carried{};//to pass over still, and what the pages before returned at _start.
		optional<UA_DateTime> _last;
		uint _lastCount{};
	};
	//historyEvents( opc, node, start, end, select, limit, continuation ){ status continuation events{ fields } } (spec *Events*):  a
	//server's own event history of one node, read for the caller over the caller's session, as `history` reads its values (spec
	//*Pass-through*) - Part 11's ReadEventDetails from start to end, through the same async service, with an EventFilter of the
	//eight base fields then the browse paths `select` names, as a SubscribeEvents asks them.  Each event answers as its field
	//list in that order, a field the server left empty null.  At most `limit` events a call, readLimit by default;  a
	//continuation point the server returns is released before the answer, since the session would lose it, and `continuation`
	//is an EventsCursor's text, which the next call passes back to read on from.  `status` is the node's HistoryReadResult's:  Bad_HistoryOperationUnsupported from a server
	//that keeps no event history, the embedded OpcServer among them, or the service's own Bad_NotSupported or Bad_ServiceUnsupported from one with no history service.
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
			EventsCursor Cursor;
		};
		α Read( const Args& args, const UA_HistoryReadResult& result )ι->jarray;//the events the cursor doesn't pass over, as field lists.
		QL::TableQL _query;
		sp<UAClient> _client;
		EventsCursor _cursor;
	};
}
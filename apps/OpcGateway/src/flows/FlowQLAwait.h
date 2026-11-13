#pragma once
#include <jde/ql/IQLAwaitExe.h>
#include <jde/flows/FileTrace.h>
#include "FlowRows.h"

namespace Jde::Opc::Gateway{
	//The trace's reads (docs/flows/spec.md *History*), over the flow's day files (FlowHost::Store):  each needs Read on the
	//flow, and answers at most /gateway/flows/readLimit records, a page, with the opaque continuation the history reads
	//carry.  A time is an ISO 8601 string, as a message's `_time` is;  an id a message's `_id`, as the web holds it, a
	//Long, or its decimal text.  A 64-bit integer goes out as the web's Long, in a record's `msgId` and `parentId` and
	//inside its json.
	//flowTrace( flow, start, end, node, msg, kind, run, limit, continuation ){ continuation records{ time msgId parentId runId
	//node kind ports json cut error status{ fill shape text } blocks } }:  the records in the range, oldest first from `start`,
	//newest first back from `end` with no `start`, and in their order with both, each bound inclusive;  narrowed to a node's
	//id, a message, a kind (hop, birth, clone, error, status, drop) or a run.  `json`:  the message where the record carries
	//it, or its cut text with `cut` true.  `continuation`:  for the next page, null on the last.
	//flowMessage( flow, id, start, end, limit ){ more records{ … } }:  the message's lineage, its ancestors' records up to the
	//source, its own and its descendants', oldest first, over the days from `start`, or the day of the gateway's start the id
	//carries, to `end`, or today.  `more`:  the limit cut it short.
	//flowCounts( flow, start, end, by ){ counts{ time node received sent drops errors } }:  the minute records from `start` to
	//before `end`, summed per node into buckets `by` wide, an ISO 8601 duration of a minute or more;  `sent` per output port.
	struct FlowQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		FlowQLAwait( QL::TableQL&& q, QL::Creds creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName==TraceCommand || q.JsonName==MessageCommand || q.JsonName==CountsCommand; }
		static constexpr sv TraceCommand{ "flowTrace" }, MessageCommand{ "flowMessage" }, CountsCommand{ "flowCounts" };
		static constexpr array<sv,6> Tables{ TraceCommand, MessageCommand, CountsCommand, "records", "counts", "status" };//the sub-tables resolve no view.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		//The flow's row, Read on it checked, and the store;  the flows' ServiceUnavailable, a flow NotFound, and a caller refused.
		α Gate( SL sl )ε->std::pair<FlowRow,sp<Flows::TraceStore>>;
		α Trace( const FlowRow& flow, Flows::TraceStore& store, SL sl )ε->jobject;
		α Message( const FlowRow& flow, Flows::TraceStore& store, SL sl )ε->jobject;
		α Counts( const FlowRow& flow, Flows::TraceStore& store, SL sl )ε->jobject;
	};
}
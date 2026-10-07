#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Read.pb.h>
ENABLE_WARNINGS

namespace Jde::QL{ struct Input; struct TableQL; }
//The `hist` QL field's arguments, continuation and result (spec *Reads*), which a read of a server's own history with
//`opc` (HistQLAwait, spec *Pass-through*) and a read of a group's with `group` (Phase 5) share:  one result shape, one
//continuation form, so the web draws both with the same components.
namespace Jde::Opc::Gateway::HistQL{
	//hist( opc, nodes, start, end, modified, returnBounds, limit, continuation ), checked:  `opc` and not `group`, at
	//least one node, a start or an end, and limit at most readLimit.  Times are UA ticks.  Continuation is decoded and
	//checked against the other arguments.
	struct Args final{
		Args( const QL::Input& input, SRCE )ε;
		α Reverse()Ι->bool{ return Start && End ? *Start>*End : !Start; }//an end alone reads backward from it.
		//The CRC-32C of every argument but limit, which a continuation carries:  the same read pages at any size, and one
		//passed with other arguments is refused.
		α Crc()Ι->uint32_t;
		string Opc;
		vector<NodeId> Nodes;
		optional<UA_DateTime> Start, End;
		bool Modified{};
		bool Bounds{};
		uint Limit{};
		optional<Hist::Proto::Continuation> Continuation;//the page before's.
	};
	//The most values a page holds:  /gateway/hist/readLimit, 10,000 by default.
	α ReadLimit()ι->uint;
	//The continuation as the caller carries it, base64url of the proto, and back.  Decode throws a UAException with
	//Bad_ContinuationPointInvalid for text that isn't a continuation, one for other nodes, or one of a read with other
	//arguments, as Group::Read does.
	α Encode( const Hist::Proto::Continuation& c )ι->string;
	α Decode( sv text, uint32_t crc, uint nodes, SL sl )ε->Hist::Proto::Continuation;

	//A value as the result carries it:  its node's place in Args::Nodes, the value, whether it is one of the bounds asked
	//for, whether it is a heartbeat, which a server's never is, and for a modified read its ModificationInfo.
	struct Modification final{ UA_DateTime Time; UA_HistoryUpdateType Type; string User; };
	struct ReadValue final{
		uint Slot;
		Value Data;
		bool Bound{};
		bool Heartbeat{};
		optional<Modification> Modified;
	};
	//values{ node source server status value bound heartbeat modification{ time type user } } in the read's order,
	//continuation for the next page, null on the last, and nodes{ node status } with each node's status as the server
	//answered it, Good for a group's.  Only the columns the query names.
	α ToJson( const QL::TableQL& ql, const vector<NodeId>& nodes, vector<ReadValue>&& values, sv continuation, const vector<StatusCode>& statuses )ι->jvalue;
}
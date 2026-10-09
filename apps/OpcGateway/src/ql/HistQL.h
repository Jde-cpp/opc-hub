#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Read.pb.h>
ENABLE_WARNINGS

namespace Jde::QL{ struct Input; struct TableQL; }
//The `history` QL field's arguments, continuation and result (spec *Reads*), which a read of a server's own history with
//`opc` (HistQLAwait, spec *Pass-through*) and a read of a group's with `group` (Phase 5) share:  one result shape, one
//continuation form, so the web draws both with the same components.
namespace Jde::Opc::Gateway::HistQL{
	//history( opc, nodes, start, end, modified, returnBounds, limit, continuation ), checked:  `opc` and not `group`, at
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
	//The most values a page holds:  /gateway/hist/readLimit, 10,000 by default, and 0 for no limit.
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

	//The edit fields (spec *Edits*), mutations by their whole names (QL::SetSystemMutations), indexed by EEdit:  Part 11's
	//Insert, Replace and Update, and a purge, by range or at times.
	enum class EEdit : uint8{ Insert, Replace, Update, Purge };
	constexpr array<sv,4> EditCommands{ "createHistory", "updateHistory", "upsertHistory", "purgeHistory" };
	α FindEdit( sv command )ι->optional<EEdit>;
	//Whose history an edit names:  the server's own with `opc` (spec *Pass-through*), or a group's with `group` (Phase 5).
	//Neither is a call that names both, or neither, which TargetRefused refuses.  A null names nothing.
	enum class ETarget : uint8{ Opc, Group, Neither };
	α Target( const QL::Input& input )ι->ETarget;
	α TargetRefused( EEdit edit, SRCE )ι->Exception;
	//A value createHistory, updateHistory or upsertHistory writes:  its node's place in EditArgs::Nodes, and the DataValue's parts as
	//the caller gave them, the value to be typed by the node's DataType.
	struct EditValue final{ uint Slot; jvalue Data; optional<UA_DateTime> Source; optional<UA_DateTime> Server; optional<StatusCode> Status; };
	//createHistory|updateHistory|upsertHistory( opc, values:[{ node source server status value }] ) and purgeHistory( opc,
	//nodes, start, end ) or purgeHistory( opc, nodes, times ), checked:  `opc` and not `group`, a value or a node, and a
	//purge's range or times, not both.  Nodes holds each node once, in the order named.  Times are UA ticks.
	struct EditArgs final{
		EditArgs( EEdit edit, const QL::Input& input, SRCE )ε;
		α Command()Ι->sv{ return EditCommands[(uint8)Edit]; }
		EEdit Edit;
		string Opc;
		vector<NodeId> Nodes;
		vector<EditValue> Values;//the UpdateData's.
		UA_DateTime Start{}, End{};//a range purge's.
		vector<UA_DateTime> Times;//a purge at times', empty for a range.
	private:
		α Slot( NodeId&& node )ι->uint;
	};
	//An edited value's, or time's, result:  its node's place in EditArgs::Nodes, its source time, and its status.
	struct EditResult final{ uint Slot; optional<UA_DateTime> Time; StatusCode Status; };
	//values{ node source status } in the order the edit names them, a purge at times' node by node, and nodes{ node status }
	//with each node's entry status.  Only the columns the request names.
	α ToJson( const QL::TableQL& ql, const vector<NodeId>& nodes, const vector<EditResult>& values, const vector<StatusCode>& statuses )ι->jvalue;
}
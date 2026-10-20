#pragma once
#include <jde/ql/IQLAwaitExe.h>
#include "../hist/HistRows.h"

namespace Jde::Opc::Gateway{
	struct HistGroupHost;
	//The web's gate on a group's history (spec *Reads*):  three fields beside `history` that read no history.
	//historyGroups( opc, nodes ){ groups{ id name nodes } }:  the groups on the connection that hold any of the nodes, each with
	//those it holds, spelled as a read spells them back, among the groups the caller may read.  A node is in one group per
	//connection at most, so this says where the gateway keeps each node's history.  A member the collector hasn't resolved by
	//its server's index yet, as before its first connect, and a historian that isn't running, answer none.
	//historyCapabilities( group ){ aggregates update delete }:  the aggregates a group computes (Hist::Aggregates), and
	//whether the caller may edit its history - Update for createHistory, updateHistory and upsertHistory, Delete for
	//purgeHistory - each a grant on an enforced resource, as the edits are gated (spec *Authorization*).  Needs Read on the
	//group, as a read does, and answers ServiceUnavailable without the historian.
	//historyThresholds( group ){ update nodes{ id node templateNode path enumeration thresholds{ name value source } } }:  the
	//group's member list (#338) - each hist_group_nodes row, its node by the server's index once the collector has it, so a
	//page keys it as its variables are keyed, its template member and path, whether the server said its DataType is an
	//enumeration, which takes no band whatever its row says (HistGroupHost::ForGroup), null until it has, and each threshold as
	//the row, its member and its group resolve it, with where it comes from (ResolveThresholdSources);  and `update`, whether
	//updateHistGroupNode would pass the caller - the table's Update right, which an unenforced resource passes.  Needs Read on
	//the group and the historian, as historyCapabilities does.
	struct HistGroupInfoQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		HistGroupInfoQLAwait( QL::TableQL&& q, QL::Creds creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName==GroupsCommand || q.JsonName==CapabilitiesCommand || q.JsonName==ThresholdsCommand; }
		static constexpr sv GroupsCommand{ "historyGroups" }, CapabilitiesCommand{ "historyCapabilities" }, ThresholdsCommand{ "historyThresholds" };
		static constexpr array<sv,5> Tables{ GroupsCommand, CapabilitiesCommand, ThresholdsCommand, "nodes", "node" };//historyGroups{ nodes{ node } }.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		α Groups()ε->jvalue;
		α Capabilities()ε->jvalue;
		//The group the caller may read, as this gateway hosts it (HistHost::Gate):  ServiceUnavailable without the historian, NotFound for one it doesn't host.
		α Host( HistGroupPK group )ε->sp<HistGroupHost>;
		α Thresholds( const HistGroupRow& group, const vector<HistGroupNodeRow>& rows, const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, const HistGroupHost& host )ε->jvalue;
	};
}
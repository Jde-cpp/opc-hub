#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//The web's gate on a group's history (spec *Reads*):  two fields beside `history` that read nothing.
	//historyGroups( opc, nodes ){ groups{ id name nodes } }:  the groups on the connection that hold any of the nodes, each with
	//those it holds, spelled as a read spells them back, among the groups the caller may read.  A node is in one group per
	//connection at most, so this says where the gateway keeps each node's history.  A member the collector hasn't resolved by
	//its server's index yet, as before its first connect, and a historian that isn't running, answer none.
	//historyCapabilities( group ){ aggregates update delete }:  the aggregates a group computes (Hist::Aggregates), and
	//whether the caller may edit its history - Update for createHistory, updateHistory and upsertHistory, Delete for
	//purgeHistory - each a grant on an enforced resource, as the edits are gated (spec *Authorization*).  Needs Read on the
	//group, as a read does, and answers ServiceUnavailable without the historian.
	struct HistGroupInfoQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		HistGroupInfoQLAwait( QL::TableQL&& q, QL::Creds creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName==GroupsCommand || q.JsonName==CapabilitiesCommand; }
		static constexpr sv GroupsCommand{ "historyGroups" }, CapabilitiesCommand{ "historyCapabilities" };
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		α Groups()ε->jvalue;
		α Capabilities()ε->jvalue;
	};
}
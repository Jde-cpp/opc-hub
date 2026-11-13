#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//Every edit of the flows' four tables (opcGateway-meta.jsonnet, docs/flows/spec.md *Flows*, *Rights*), routed here by
	//Gateway::CustomMutation, and HubQL's.
	//A flow's create, update, delete, restore and purge:  the table's rights, then, for a row that is there, the same right on
	//the flow's own access row, keyed by its guid, else the table's again (Authorize::Test);  the row's checks, a slug of a
	//file name's characters, since it is the flow's trace directory, and a trace level that is one;  the guid, spelled here at
	//the create;  the stock op;  and the live side (FlowHost), which starts the flow or stops it as its row now says.  A
	//purge's live side runs before its statement, which the flow's versions, runs and context rows would otherwise refuse,
	//and after it the trace directory and the flow's access row go.
	//flow_versions, flow_contexts and flow_runs take no generic mutation, whatever the caller's rights:  deployFlow writes the
	//first with its deployer, the host the second as the flows set their context, and a replay the third.
	//deployFlow( flow, graph, comment ){ flow version deployed userName running dropped fault{ node message } },
	//injectFlow( flow, node, message ){ flow node } and setFlowContext( flow, node, name, value ){ flow node name value },
	//system mutations by their whole names, so only a POST or the socket runs them, each under Update on the flow, the
	//table's for the gateway's context (FlowHost::Deploy, Inject, SetContext).
	struct FlowMAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		static constexpr sv DeployCommand{ "deployFlow" }, InjectCommand{ "injectFlow" }, ContextCommand{ "setFlowContext" };
		FlowMAwait( QL::MutationQL&& m, QL::Creds creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;
		Ω Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}
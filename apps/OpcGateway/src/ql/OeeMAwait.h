#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//Every edit of OEE's six tables (opcGateway-meta.jsonnet, docs/oee/spec.md *Machines*, *Reasons and codings*, *Authorization*),
	//routed here by Gateway::CustomMutation, and HubQL's, ahead of HistGroupMAwait:  it also claims a hist_groups delete or purge
	//while a machine holds the group, and a hist_group_nodes purge while a signal names the member, and refuses them before the
	//historian's await runs its live side ahead of the statement, which would take the group from the historian, and the
	//machine's subscription with it, before the foreign key failed.  Those it decides from OeeHost's rows in memory, since Test
	//is synchronous and reads none;  a group's delete keyed by its slug is claimed while any machine stands, and handed to
	//HistGroupMAwait once its row shows no machine over it.
	//In order:  a generic mutation of oee_stops, oee_intervals or oee_codings refused whatever the caller's rights, since the
	//engine writes the first two and codeOee and uncodeOee the third;  the table's rights;  the row's checks, libs/oee's rules
	//on the machine and its signals as the mutation would leave them, a signal set left empty passing, a signal's member in
	//the machine's group, a calendar the client's cache holds, a reason's code unique and its purge refused while a coding
	//names it;  the stock op;  and the live side (OeeHost).  A machine's purge runs its live side first, for its rows go
	//before the statement.  createOeeMachine takes `from`, a time of no column, where the engine starts:  now when absent, and
	//never after it.  A signal's create or update answers `warnings`, a list:  a counter whose member has a deviation band,
	//and a member whose MinTimeInterval is past `settle`.  Every refusal is a BadRequest;  a row that isn't there is NotFound.
	//A signal's update that changes its `running` or `ratePeriod`, and a machine's that changes its `idleTimeout`, the engine's
	//inputs, answer `recompute: true` with a warning that says so:  the rows written before it were made with the old one.
	//recomputeOee( machine, start, end ){ machine start end }, a system mutation registered by its whole name (spec *The
	//engine*, *Authorization*):  the machine's rows over the span made again from the group's history (OeeHost::Recompute),
	//for the caller with Update on oee_machines and Read on the machine's group through HistHost::Gate.  It answers once the
	//rebuild is queued, not done.
	struct OeeMAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		static constexpr sv RecomputeCommand{ "recomputeOee" };
		OeeMAwait( QL::MutationQL&& m, QL::Creds creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;
		Ω Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}
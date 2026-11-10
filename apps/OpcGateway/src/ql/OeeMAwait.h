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
	//codeOee( machine, start, end, reason, comment ){ id start end reason{ id name category } comment userName time } and
	//uncodeOee( id ){ id }, system mutations too (spec *Reasons and codings*, *Authorization*), so only a POST or the socket runs
	//them:  an operator's reason over a range of the machine's time, and its removal.  codeOee needs Update on oee_codings and
	//uncodeOee Delete, the table's, which an unenforced resource passes (*Open questions* #7), each with Read on the machine's
	//group through HistHost::Gate.  A machine's codings do not overlap:  one that would is refused, but for the coding at the
	//same start, which is replaced, checked against the others as an insert is.  The reason is one that isn't deleted, and the
	//row's identity, name and time are the caller's and now, never an argument's.
	struct OeeMAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		static constexpr sv RecomputeCommand{ "recomputeOee" }, CodeCommand{ "codeOee" }, UncodeCommand{ "uncodeOee" };
		OeeMAwait( QL::MutationQL&& m, QL::Creds creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;
		Ω Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}
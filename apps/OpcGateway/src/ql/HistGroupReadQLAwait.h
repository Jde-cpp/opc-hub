#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//history with `group`:  a group's own history, from its files and its buffer (spec *Reads*), raw, at times or aggregated, by
	//Group::Read, ReadAtTime and ReadProcessed.  It needs Read on the group, its own access_resources row else hist_groups'
	//(spec *Authorization*), which passes where nothing enforces either.  Each node named maps to the member that holds it now,
	//and one that isn't a member answers Bad_NodeIdUnknown, with no values;  a read none of whose pages held a value answers
	//Good_NoData for each of its nodes.  An at-time read takes simple bounds, as the pass-through asks a server for them, and
	//both computed modes the AggregateConfiguration at Part 13's defaults, as OpcServer publishes them.  The continuation is
	//the library's.  While the gateway runs without its historian, every read answers ServiceUnavailable (HistHost::TestEnabled).
	struct HistGroupReadQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		HistGroupReadQLAwait( QL::TableQL&& q, QL::Creds creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool;//history naming no `opc`:  a group's, or one this refuses for naming neither.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}
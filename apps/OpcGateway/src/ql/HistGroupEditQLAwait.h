#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//createHistory, updateHistory, upsertHistory and purgeHistory with `group` (spec *Edits*):  Group::Edit, by the caller, who
	//needs Update on the group, or Delete for a purge, granted on a resource that is enforced, the group's own access_resources
	//row else hist_groups' (Authorize::TestEnforced, spec *Authorization*):  where nothing enforces either, every edit is
	//refused, Forbidden.  A value takes the type the group stored for its node (Group::DataType), so a correction needs no
	//server, or with none stored the type its json implies;  one that can't take it refuses the call.  The library flushes the
	//group's buffer first and resumes once the edit's records are durable.  Each value's result is the library's, and a node
	//that isn't a member answers Bad_NodeIdUnknown for its entry and each of its values.  While the gateway runs without its
	//historian, every edit answers ServiceUnavailable (HistHost::TestEnabled).
	struct HistGroupEditQLAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		HistGroupEditQLAwait( QL::MutationQL&& m, QL::Creds creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;//an edit field with `group` and no `opc`.
		Ω Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}
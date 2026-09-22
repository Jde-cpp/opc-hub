#pragma once
#include <jde/ql/QLHook.h>

namespace Jde::Opc::Gateway{
	struct OpcQLHook : QL::IQLHook{
		α InsertBefore( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α InsertFailure( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α PurgeBefore( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α PurgeFailure( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α UpdateAfter( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;//the live clients an edit or delete leaves stale - reviews/m3-closing.md #4.
		α PurgeAfter( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
	};
}
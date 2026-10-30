#pragma once
#include <jde/ql/QLHook.h>

namespace Jde::Opc::Gateway{
	struct ServerCnnctn;
	//`base` with every connection setting `input` names written over it (docs/clients/spec.md *Connection settings*):  the certificate
	//URI, the mode, the policy, the four durations and the locale, a null clearing one, strings trimmed as the web trims them.  The
	//merged row the save rules read (OpcQLHook's InsertBefore over a blank row, UpdateBefore over the saved one) and testConnection's
	//throwaway row.  Read through Find/As, never Args:  a value bound through a variable is its `\b$name` marker there.  A duration
	//that is not a whole number of milliseconds is refused here, BadRequest, before the engine's own conversion sees it.
	α MergeSettings( const QL::Input& input, ServerCnnctn base, SL sl )ε->ServerCnnctn;
	struct OpcQLHook : QL::IQLHook{
		α InsertBefore( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α UpdateBefore( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;//the connection settings' save rules over the merged row (spec *Connection settings*).
		α InsertAfter( const QL::MutationQL& m, UserPK executer, uint id, SRCE )ι->HookResult override;//the endpoint's folder for the new row (server/GatewayServer.h).
		α InsertFailure( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α PurgeBefore( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α PurgeFailure( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
		α UpdateAfter( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;//the live clients an edit or delete leaves stale - reviews/m3-closing.md #4.
		α PurgeAfter( const QL::MutationQL& m, UserPK executer, SRCE )ι->HookResult override;
	};
}
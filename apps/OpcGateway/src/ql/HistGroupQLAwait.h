#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//Every edit of the historian's four tables (opcGateway-meta.jsonnet, spec *Collection* and *Templates*), routed here by
	//GatewayQL::CustomMutation, and HubQL's:  authorized first, then the row's checks, the columns the gateway spells - a group's
	//guid and root, a node's node and connection - the stock op, and its live side (HistHost):  the group, its members, their
	//thresholds and their items.  A purge's live side runs before its statement, which the row's children would otherwise
	//refuse;  the rest runs after.  The columns a row is keyed by in the files or the server - a group's connection, template
	//and root, a node's node, a member's path - never change:  purge the row and create it again.
	//resolveHistory( group ){ nodes{ templateNode path node status } }, a mutation by its whole name that needs Update on the
	//group (spec *Authorization*), retries the group's unresolved template members.
	struct HistGroupMAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		HistGroupMAwait( QL::MutationQL&& m, QL::Creds creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;
		Ω Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
		static constexpr sv ResolveCommand{ "resolveHistory" };
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}
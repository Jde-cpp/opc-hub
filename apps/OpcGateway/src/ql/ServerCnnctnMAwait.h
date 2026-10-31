#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	struct ServerCnnctn;
	//`base` with every connection setting `input` names written over it (docs/clients/spec.md *Connection settings*):  the certificate
	//URI, the mode, the policy, the four durations and the locale, a null clearing one, strings as sent - the engine stores them
	//untrimmed, so the rules judge them so.  The merged row the save rules read (ServerCnnctnMAwait's create over a blank row, its
	//update over the saved one) and testConnection's throwaway row.  Read through Find/As, never Args:  a value bound through a
	//variable is its `\b$name` marker there.  A duration that is not a whole number of milliseconds, or is over
	//ServerCnnctn::MaxDurationMs, is refused here, BadRequest.
	α MergeSettings( const QL::Input& input, ServerCnnctn base, SL sl )ε->ServerCnnctn;

	//The provider row a connection's slug logs in under (auth/UM.h), kept in step with the row:  made before the insert, taken
	//back when the insert fails - unless the slug is a saved row's, which is the refusal that replaces the engine's
	//(reviews/install-issues.md #51) - taken before the purge and made again when the purge fails.
	enum class EProviderStep : uint8{ InsertBefore, InsertFailure, PurgeBefore, PurgeFailure };

	//Every write to server_connections (opcGateway-meta.jsonnet), routed here by GatewayQL::CustomMutation, and HubQL's:
	//authorized first, a create's engine refusals next (QL::CheckCreate), then the row's rules - the slug's (ServerCnnctn::SlugError) on a create, the connection settings' (spec
	//*Connection settings*) on a create over a blank row and on an update over the saved one - the provider row's step, once the
	//schema's sync is done and the AppServer is reachable (#520 review #4), the stock op, and its live side:  the endpoint's folder for a new row (server/GatewayServer.h), and the clients an edit, a delete or a
	//purge leaves stale (connectionEdited).  A failed insert or purge takes its provider step back before the engine's error is
	//reported.
	struct ServerCnnctnMAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		ServerCnnctnMAwait( QL::MutationQL&& m, QL::Creds creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;
		Ω Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
		Ω SetSynced( bool synced=true )ι->void;//Gateway::Configure, last:  the schema's sync, and the seed inside it, is done.  The tests clear it.
		Ω ProviderStep( const QL::MutationQL& m, EProviderStep step, SRCE )ι->up<TAwait<jvalue>>;//one step on its own - the tests drive the two failure steps (ServerCnnctnDBTests).
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}
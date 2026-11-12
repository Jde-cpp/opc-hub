#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	struct ServerCnnctn;
	//`base` with what `input` names of the columns the save rules read written over it:  the url, the kind and its `driver`
	//(docs/open-protocol/spec.md *The row*), and the connection settings (docs/clients/spec.md *Connection settings*), the certificate
	//URI, the mode, the policy, the four durations and the locale;  a null clearing one, strings as sent - the engine stores them
	//untrimmed, so the rules judge them so.  The merged row the save rules read (ServerCnnctnMAwait's create over a blank row, its
	//update over the saved one) and testConnection's throwaway row.  Read through Find/As, never Args:  a value bound through a
	//variable is its `\b$name` marker there.  A duration that is not a whole number of milliseconds, or is over
	//ServerCnnctn::MaxDurationMs, is refused here, BadRequest.
	α MergeSettings( const QL::Input& input, ServerCnnctn base, SL sl )ε->ServerCnnctn;
	//The save's rules over `row`, the merged row, and testConnection's over its throwaway one:  a certificate URI, a default browse
	//namespace, a mode or a policy that `input` names with a value on a kind with a driver, which fixes all four;  the kind's own
	//(ServerCnnctn::KindError);  the connection settings' (ServerCnnctn::SettingsError).  Null for a row that saves.
	α RowError( const QL::Input& input, const ServerCnnctn& row, SL sl )ι->up<Exception>;

	//The provider row a connection's slug logs in under (auth/UM.h), kept in step with the row:  made before the insert, taken
	//back when the insert fails - unless the slug is a saved row's, which is the refusal that replaces the engine's
	//(reviews/install-issues.md #51) - taken before the purge and made again when the purge fails.
	enum class EProviderStep : uint8{ InsertBefore, InsertFailure, PurgeBefore, PurgeFailure };

	//Every write to server_connections (opcGateway-meta.jsonnet), routed here by GatewayQL::CustomMutation, and HubQL's:
	//authorized first, a create's engine refusals next (QL::CheckCreate), then the row's rules - the slug's
	//(ServerCnnctn::SlugError) on a create, the kind's and the connection settings' (RowError) on a create over a blank row and on
	//an update over the saved one, where a kind changed to a driver's also clears the four columns that kind fixes - the provider
	//row's step, once the schema's sync is done and the AppServer is reachable (#520 review #4), the stock op, and its live side:
	//the endpoint's folder for a new row (server/GatewayServer.h), the clients an edit, a delete or a purge leaves stale
	//(connectionEdited), and a driver row's own server (drivers/DriverHost.h), started, restarted or stopped with its row.  A
	//failed insert or purge takes its provider step back before the engine's error is reported.
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
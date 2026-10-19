#pragma once
#include <jde/ql/LocalQL.h>
#include <jde/app/AppQL.h>

namespace Jde::App::Server{
	struct AppServerQL;
	α QLPtr()ι->sp<QL::LocalQL>;
	α QL()ι->QL::LocalQL&;
	α ConfigureQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer )ι->void;
	α SetQL( sp<QL::LocalQL> ql )ι->void;//a host's own QL over the app schemas and more (OpcHub's HubQL) - QLPtr() consumers see that one.
	//The AppServer's custom arms, which AppServerQL and a host's QL (HubQL) both run before their own.  A mutation asks the QL's
	//own LogSettingsMutation first:  it gates on that QL's log schema.
	α CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>;//access, connections, settings, instanceTagLevel, instance sessions, session(s).
	α CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>;//access, instanceTagLevel, purgeSession.

	struct AppServerQL final: App::AppQL{
		AppServerQL( vector<sp<DB::AppSchema>>&& schemas, sp<Access::Authorize>&& authorizer )ι;
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
	};
}
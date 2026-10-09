#pragma once
#include <jde/app/AppQL.h>

namespace Jde::Opc::Hub{
	//One QL over access + app + gateway: the AppServer's custom queries/mutations first (they own the app/access names), then
	//the gateway's for what it claims (GatewayQLAwait::IsApplicable - opcSessions, search, anything keyed by `opc`).  Installed
	//into both apps' QL singletons (App::Server::SetQL, Gateway::SetQL), so every existing QLPtr() consumer - the app socket, the
	//gateway socket, ClientQuery, the search/opcSessions awaits - serves all three schemas from it.  `access` stays first: the
	//__schema introspection answers from schemas[0], as the AppServer's does.
	struct HubQL final : App::AppQL{
		HubQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer )ι;
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject override;
	};
}

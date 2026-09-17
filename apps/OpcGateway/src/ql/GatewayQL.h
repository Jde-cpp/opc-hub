#pragma once
#include <jde/app/AppQL.h>

namespace Jde::Opc::Gateway{
	struct GatewayQL;
	α QLPtr()ι->sp<QL::LocalQL>;
	α QL()ι->QL::LocalQL&;
	α ConfigureQL( sp<DB::AppSchema> schema, sp<Access::Authorize> authorizer )ι->void;
	//A host serving the gateway schema from its own QL (OpcHub's HubQL, over access+app+gateway): every QLPtr() consumer -
	//the gateway socket, ClientQuery, the search/opcSessions awaits - then answers from that one.
	α SetQL( sp<QL::LocalQL> ql )ι->void;
	α Schemas()ι->const vector<sp<DB::AppSchema>>&;
	α AddStatusCounts( jobject& status )ι->void;//the gateway's `clients`/`monitoredItems`/`pendingItems` on a status document - GatewayQL's and HubQL's StatusQuery.

	struct GatewayQL final: App::AppQL{
		GatewayQL( sp<DB::AppSchema>&& schema, sp<Access::Authorize> authorizer )ι;
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override;
		α StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject override;
	};
}
#include "HubQL.h"
#include <jde/db/meta/AppSchema.h>
#include "../../../AppServer/src/LocalClient.h"
#include "../../../AppServer/src/appStartup.h"
#include "../../../AppServer/src/ql/AppServerQL.h"
#include "../../../OpcGateway/src/ql/GatewayQL.h"

namespace Jde::Opc::Hub{
	HubQL::HubQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer )ι:
		App::AppQL{ move(schemas), move(authorizer), App::Server::AppSchema()->Name, App::HiddenLogInstances }//the AppServer's log, as AppServerQL
	{}
	α HubQL::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = App::Server::CustomQuery(q, creds, sl); await )//nothing the gateway also names.
			return await;
		return Gateway::CustomQuery( q, move(creds), sl );
	}
	α HubQL::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = LogSettingsMutation(m, creds, App::Server::AppClient(), sl); await )//covers the gateway's plural spelling too.
			return await;
		if( auto await = App::Server::CustomMutation(m, creds, sl); await )//its purgeSession never comes from the hub's own gateway - HubLogout removes from the one map.
			return await;
		return Gateway::CustomMutation( m, move(creds), sl );
	}
	α HubQL::StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject{
		auto y = App::AppQL::StatusQuery( move(ql), executer, sl );//the base gates it.
		Gateway::AddStatusCounts( y );
		return y;
	}
}
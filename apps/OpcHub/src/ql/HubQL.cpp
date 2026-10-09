#include "HubQL.h"
#include <jde/access/server/accessServer.h>
#include "../../../AppServer/src/LocalClient.h"
#include "../../../AppServer/src/ql/AppQLAwait.h"
#include "../../../AppServer/src/ql/InstanceTagLevelAwait.h"
#include "../../../OpcGateway/src/ql/GatewayQL.h"
#include "../../../OpcGateway/src/ql/GatewayQLAwait.h"

namespace Jde::Opc::Hub{
	HubQL::HubQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer )ι:
		App::AppQL{ move(schemas), move(authorizer) }
	{}
	α HubQL::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = App::Server::AppQLAwait::Test(q, creds, sl); await )//access custom, connections, settings, instanceTagLevel - nothing the gateway also names.
			return await;
		return Gateway::GatewayQLAwait::IsApplicable(q) ? Gateway::GatewayQLAwait::Test( q, move(creds), sl ) : nullptr;
	}
	α HubQL::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = LogSettingsMutation(m, creds, App::Server::AppClient(), sl); await )//covers the gateway's plural spelling too.
			return await;
		if( auto await = Access::Server::CustomMutation(m, creds, sl); await )
			return await;
		if( App::Server::InstanceTagLevelMAwait::IsApplicable(m) )
			return mu<App::Server::InstanceTagLevelMAwait>( move(m), creds.UserPK(), sl );
		return Gateway::GatewayQLMAwait::IsApplicable(m) ? mu<Gateway::GatewayQLMAwait>( move(m), move(creds), sl ) : nullptr;
	}
	α HubQL::StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject{
		auto y = App::AppQL::StatusQuery( move(ql), executer, sl );//the base gates it.
		Gateway::AddStatusCounts( y );
		return y;
	}
}

#include "GatewayQL.h"
#include "GatewayQLAwait.h"
#include <jde/app/client/awaits/LogSettingsClientAwait.h>
#include "../UAClient.h"

namespace Jde::Opc{
	namespace Gateway{ sp<QL::LocalQL> _ql; }
	α Gateway::QLPtr()ι->sp<QL::LocalQL>{ ASSERT(_ql); return _ql; }
	α Gateway::QL()ι->QL::LocalQL&{ return *QLPtr(); }
	α Gateway::Schemas()ι->const vector<sp<DB::AppSchema>>&{ return QL().Schemas(); }
	α Gateway::ConfigureQL( sp<DB::AppSchema> schema, sp<Access::Authorize> authorizer )ι->void{
		QL::Configure( {schema} );//once: the ctor used to repeat it, registering every gateway type's introspection twice.
		_ql = ms<GatewayQL>( move(schema), move(authorizer) );
	}
	α Gateway::SetQL( sp<QL::LocalQL> ql )ι->void{ _ql = move(ql); }
	α Gateway::AddStatusCounts( jobject& status )ι->void{
		const auto [clients, monitoredItems, pendingItems] = UAClient::StatusCounts();
		status["clients"] = clients;
		status["monitoredItems"] = monitoredItems;
		status["pendingItems"] = pendingItems;//subscriptions a lost connection parked for its reconnect - monitoredItems counts only what is on a client.
	}
}
namespace Jde::Opc::Gateway{
	GatewayQL::GatewayQL( sp<DB::AppSchema>&& schema, sp<Access::Authorize> authorizer )ι:
		App::AppQL{ {move(schema)}, authorizer }
	{}
	α GatewayQL::CustomQuery( QL::TableQL& q, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>>{
		up<TAwait<jvalue>> await = GatewayQLAwait::Test( q, executer, sl );
		return await;
	}
	α GatewayQL::CustomMutation( QL::MutationQL& m, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>>{
		up<TAwait<jvalue>> await = GatewayQLMAwait::Test( m, executer, sl );
		return await;
	}
	α GatewayQL::LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>>{
		RequireAuthenticated( executer, "logSettings", sl );
		return mu<App::Client::LogSettingsClientAwait>( move(ql), sl );
	}
	α GatewayQL::StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject{
		auto y = App::AppQL::StatusQuery( move(ql), executer, sl ); //the base gates it.
		AddStatusCounts( y );
		return y;
	}

}
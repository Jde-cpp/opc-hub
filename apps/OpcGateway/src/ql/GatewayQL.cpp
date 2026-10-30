#include "GatewayQL.h"
#include "GatewayQLAwait.h"
#include "HistGroupEditQLAwait.h"
#include "HistGroupQLAwait.h"
#include "ServerCnnctnMAwait.h"
#include "TestConnectionQLAwait.h"
#include "../GatewayAppClient.h"
#include "../UAClient.h"
#include "../hist/HistHost.h"

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
		//The historian, for the soak (apps/OpcGateway/soak):  how many groups are hosted, the memory every group's buffer
		//holds together (maxBuffer caps it), and the gateway's own AppServer user - the identity its collector presents to
		//an OPC server (a Certificate token, the app client's own cert), which an enforcing OpcServer must grant Read and
		//Subscribe before a group fills.  0 groups with a hist block says the historian is off (its lock is held elsewhere).
		auto& hist = HistHost::Instance();
		status["historianGroups"] = hist.Groups().size();
		auto historian = hist.Historian();
		status["historianBytes"] = historian ? historian->Buffered() : 0;
		status["historianUser"] = AppClient()->UserPK().Value;
	}
	α Gateway::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		return GatewayQLAwait::Test( q, move(creds), sl );
	}
	α Gateway::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = HistGroupMAwait::Test(m, creds, sl); await )//the historian's group tables and resolveHistory.
			return await;
		if( auto await = HistGroupEditQLAwait::Test(m, creds, sl); await )//a group's history edits.
			return await;
		if( auto await = TestConnectionQLAwait::Test(m, creds, sl); await )
			return await;
		if( auto await = ServerCnnctnMAwait::Test(m, creds, sl); await )//a connection row's rules, provider row and live side.
			return await;
		return GatewayQLMAwait::Test( m, move(creds), sl );
	}
}
namespace Jde::Opc::Gateway{
	GatewayQL::GatewayQL( sp<DB::AppSchema>&& schema, sp<Access::Authorize> authorizer )ι:
		App::AppQL{ {move(schema)}, authorizer, {} }//no `logs` resource:  the hub mounts this schema too, and its log is `app/logs`.
	{}
	α GatewayQL::CustomQuery( QL::TableQL& q, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>>{
		return Gateway::CustomQuery( q, move(executer), sl );
	}
	α GatewayQL::CustomMutation( QL::MutationQL& m, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = LogSettingsMutation(m, executer, AppClient(), sl); await )
			return await;
		return Gateway::CustomMutation( m, move(executer), sl );
	}
	α GatewayQL::StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject{
		auto y = App::AppQL::StatusQuery( move(ql), executer, sl ); //the base gates it.
		AddStatusCounts( y );
		return y;
	}
}
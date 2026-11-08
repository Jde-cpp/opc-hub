#include "RefreshConditionsQLAwait.h"
#include <jde/ql/types/TableQL.h>
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α RefreshConditionsQLAwait::Execute()ι->TAwait<CallResult>::Task{
		try{
			NodeId node{ _mutation.As<>("node") };
			let subscription = _client->SubscriptionId();
			let monitored = _client->TryMonitoredNodes();
			if( !subscription || !monitored || !monitored->HasEventItem(node) )
				throw Exception{ _sl, ExceptionArgs{EHttpStatus::BadRequest}, "No events subscription on {} to refresh.", node.ToString() };
			DBG( "[{}]refreshConditions {}:  ConditionRefresh( {} )", hex(_client->Handle()), node.ToString(), subscription );
			vector<Variant> args;
			args.emplace_back( jvalue{static_cast<std::uint64_t>(subscription)}, &UA_TYPES[UA_TYPES_UINT32] );
			let result = co_await CallAwait{ NodeId{UA_NS0ID_CONDITIONTYPE}, NodeId{UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH}, move(args), _client, _sl };
			const QL::TableQL* request = _mutation.ResultRequest ? &*_mutation.ResultRequest : nullptr;
			jobject y;
			if( !request || request->FindColumn("statusCode") )
				y["statusCode"] = result.statusCode;
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
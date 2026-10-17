#pragma once
#include <jde/ql/types/TableQL.h>

namespace Jde::App::Server{
	//sessions( instanceId:42 ){ loginName domain endpoint lastUpdate expiration } - the web sessions a registered instance holds, for
	//its page's Web Sessions tab.  Sent through QueryInstanceAwait:  this process's own come through the local ql, where
	//SessionGraphQL (AppQLAwait) answers from the Sessions map;  another instance answers from its own (GatewayQL,
	//OpcServerAppClient::ClientQuery).  Without instanceId SessionGraphQL answers directly.
	struct InstanceSessionsAwait final : TAwait<jvalue>{
		using base = TAwait<jvalue>;
		InstanceSessionsAwait( QL::TableQL&& q, UserPK executer, SRCE )ι:base{sl}, _query{move(q)}, _executer{executer}{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName=="sessions" && q.Args.contains("instanceId"); }
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->TAwait<jvalue>::Task;
	private:
		QL::TableQL _query;
		UserPK _executer;
	};
}
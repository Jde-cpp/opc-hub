#pragma once
#include <jde/ql/types/MutationQL.h>
#include "../async/CallAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//refreshConditions( opc, node ){ statusCode } (clients spec *Alarms and conditions*, #426):  ConditionRefresh, `i=3875` on ConditionType,
	//called through the call path with the caller's client's own SubscriptionId, the method's one argument, which only the client holds -
	//one subscription per client, so every listener of it sees the replay.  `node` is the object an events item of the client's is on, the
	//Alarms view's node:  a client with no event item there, or one no listener holds any more, has nothing the replay would reach, so
	//the mutation is refused before any call, BadRequest.  The client's and not the caller's session:  this comes by REST, which names no socket.  The result is the server's status whatever it is - Bad_SubscriptionIdInvalid, Bad_RefreshInProgress, a server
	//without conditions' Bad_NodeIdUnknown - projected by the mutation's result request;  only a failure of the service throws, as
	//call's does.  Open to every session:  the OpcServer lets ConditionRefresh through for all, since it replays only what the
	//session's own subscription receives (UAAccess).
	struct RefreshConditionsQLAwait final : TAwaitEx<jvalue,TAwait<CallResult>::Task>{
		using base = TAwaitEx<jvalue,TAwait<CallResult>::Task>;
		RefreshConditionsQLAwait( QL::MutationQL&& mutation, sp<UAClient> client, SRCE )ι:base{ sl }, _mutation{move(mutation)}, _client{move(client)}{}
		static constexpr sv Command{ "refreshConditions" };//a system mutation (QL::SetSystemMutations), taken whole:  refresh is no verb the engine knows.
		α Execute()ι->TAwait<CallResult>::Task override;//the call's own type.
	private:
		QL::MutationQL _mutation;
		sp<UAClient> _client;
	};
}
#pragma once
#include <jde/ql/types/MutationQL.h>
#include "../async/CallAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//call( opc, object, method, args:[…] ){ statusCode inputArgumentResults outputArguments } (clients spec *Methods*, #318):  a system
	//mutation over the caller's own client, so the rights are the server's, which holds UserExecutable against the session's
	//credential.  Each argument is typed from the method's InputArguments, read once per method and cached with the client
	//(MethodArgumentCache), so 7 goes to an Int32 input as one and not as the Int64 its json implies;  one that will not convert
	//answers as the server answers a mistyped one, BadInvalidArgument with BadTypeMismatch in its slot, and no call is made.  A
	//method the server does not have fails as reading its class fails, BadNodeIdUnknown.  The result is the server's whatever its
	//status - a refusal is an answer the dialog shows per argument - projected by the mutation's result request.
	struct CallQLAwait final : TAwaitEx<jvalue,TAwait<CallResult>::Task>{
		using base = TAwaitEx<jvalue,TAwait<CallResult>::Task>;
		CallQLAwait( QL::MutationQL&& mutation, sp<UAClient> client, SRCE )ι:base{ sl }, _mutation{move(mutation)}, _client{move(client)}{}
		static constexpr sv Command{ "call" };//a system mutation (QL::SetSystemMutations), taken whole:  call is no verb the engine knows.
		α Execute()ι->TAwait<CallResult>::Task override;//the call's own type;  the arguments' lookup is an AnyAwait.
	private:
		QL::MutationQL _mutation;
		sp<UAClient> _client;
	};
}
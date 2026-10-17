#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/app/usings.h>
#include <jde/ql/types/TableQL.h>

namespace Jde::App::Server{
	//A query for a registered instance:  this process's own through the local ql, another's over its socket.  Nullopt when it is
	//not connected - what that means is the caller's (instanceTagLevel's `running`:  nothing running;  sessions( instanceId ):  404).
	//An AnyAwait, so a coroutine of any task type can co_await it.
	struct QueryInstanceAwait final : AnyAwait<optional<jvalue>>{
		QueryInstanceAwait( ProgInstPK instanceId, QL::TableQL&& ql, UserPK executer, SRCE )ι:
			AnyAwait<optional<jvalue>>{sl}, _instanceId{instanceId}, _ql{move(ql)}, _executer{executer}{}
	protected:
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->TAwait<jvalue>::Task;
		ProgInstPK _instanceId;
		QL::TableQL _ql;
		UserPK _executer;
	};
}
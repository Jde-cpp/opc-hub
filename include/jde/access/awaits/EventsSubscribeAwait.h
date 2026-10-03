#pragma once
#include <jde/ql/ql.h>

namespace Jde::Access{
	enum class ESubscription : uint16;
	struct AccessListener;
	struct EventTypeSubscribeAwait : VoidAwait{
		EventTypeSubscribeAwait( sp<QL::IQL> qlServer, string name, ESubscription type, string cols, string args, ESubscription events, jobject vars, UserPK executer, sp<AccessListener> listener, SRCE )ι:
			VoidAwait{sl}, _qlServer{qlServer}, _name{ move(name) }, _type{ type }, _cols{ move(cols) }, _args{ move(args) }, _events{ events }, _vars{vars}, _executer{executer}, _listener{listener}
		{}
	private:
		α Suspend()ι->void override{ Subscribe(); }
		α Subscribe()ι->TAwait<vector<QL::SubscriptionId>>::Task;
		sp<QL::IQL> _qlServer;
		string _name;
		ESubscription _type;
		string _cols;//owned: built per call now, a view would dangle across the co_awaits.
		string _args;
		ESubscription _events;
		jobject _vars;
		UserPK _executer;
		sp<AccessListener> _listener;
	};
	//schemas: the schema names the events are filtered to;  empty = every schema, with no predicate sent (ConfigureAwait::AllSchemas).
	struct EventsSubscribeAwait : VoidAwait{
		EventsSubscribeAwait( sp<QL::IQL> qlServer, vector<string> schemas, UserPK executer, sp<AccessListener> listener, SRCE )ι:
			VoidAwait{sl}, _qlServer{qlServer}, _schemas{schemas}, _executer{ executer }, _listener{listener}{}
	private:
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->EventTypeSubscribeAwait::Task;
		sp<QL::IQL> _qlServer;
		vector<string> _schemas;
		UserPK _executer;
		sp<AccessListener> _listener;
	};
}
#pragma once
#include <jde/ql/QLHook.h>
#include <jde/db/generators/Statement.h>

namespace Jde::Access::Server{
	struct UserAwait final : TAwait<jvalue>{
		UserAwait( QL::TableQL&& query, UserPK executer, SRCE )ι:
		TAwait<jvalue>{ sl },
			_query{ move(query) },
			_executer{ executer }
		{}
		α Suspend()ι->void override{ QueryGroups(); }
	private:
		α QueryGroups()ι->TAwait<jarray>::Task;
		α QueryTables( jarray groups )ι->TAwait<jvalue>::Task;
		α GroupStatement()->DB::Statement;

		QL::TableQL _query;
		UserPK _executer;
		flat_map<uint8,string> _groupColumns;
	};
}
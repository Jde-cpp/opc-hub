#pragma once
#include "IMutationAwait.h"
#include <jde/db/meta/Column.h>
#include <jde/db/generators/InsertClause.h>

namespace Jde::DB{ struct Criteria; struct Table; }
namespace Jde::QL{
	struct InsertAwait final: IMutationAwait{
		using base=IMutationAwait;
		InsertAwait( sp<DB::Table> table, MutationQL m, UserPK executer, bool identityInsert=false, SRCE )ι;//identityInsert: true only from LocalQL::Upsert, which places a row at a caller-chosen pk.
		α await_ready()ι->bool override;
		α Check()ε->void;//the Create right and the statements, built but not run (QL::CheckCreate).
		α Suspend()ι->void override{ Execute(); }
	private:
		α CreateQuery( const DB::Table& table, jobject input, bool nested=false )ε->void;
		α AddStatement( const DB::Table& table, const jobject& input, optional<DB::Criteria> criteria=nullopt )ε->void;
		α Execute()ι->TAwait<jvalue>::Task;//InsertBefore, the statements, then InsertAfter - or InsertFailure - in one coroutine.

		bool _identityInsert;
		//built by await_ready (CreateQuery), which decides from them whether there is anything to suspend for - so they are members, not Execute's locals.
		vector<vector<sp<DB::Column>>> _missingColumns;
		flat_map<string,DB::Value> _nestedIds;
		vector<DB::InsertClause> _statements;
	};
}
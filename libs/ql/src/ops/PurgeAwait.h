#pragma once
#include "IMutationAwait.h"

namespace Jde::DB{ struct Criteria; struct Sql; struct Table; }
namespace Jde::QL{
	struct PurgeAwait final: IMutationAwait{
		using base=IMutationAwait;
		using base::base;
		α await_ready()ι->bool override;
		α Suspend()ι->void override{ Execute(); }
	private:
		α Statements( const DB::Table& table, const optional<DB::Criteria>& kind={} )ε->vector<DB::Sql>;//kind: as UpdateAwait's createUpdate.
		α Execute()ι->TAwait<jvalue>::Task;//PurgeBefore, the statements, then PurgeAfter - or PurgeFailure - in one coroutine.
	};
}
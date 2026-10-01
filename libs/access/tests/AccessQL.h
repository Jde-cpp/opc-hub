#pragma once
#include <jde/ql/LocalQL.h>

namespace Jde::Access::Tests{
	struct AccessQL : QL::LocalQL{
		AccessQL( vector<sp<DB::AppSchema>>&& schemas, sp<Access::Authorize> authorizer )ι;
		virtual ~AccessQL()=default;
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α LogQuery( QL::TableQL&&, QL::Creds, SL )ε->up<TAwait<jvalue>> override{ ASSERT(false); return {}; }
		α LogSettingsQuery( QL::TableQL&&, QL::Creds, SL )ε->up<TAwait<jvalue>> override{ ASSERT(false); return {}; }
		α StatusQuery( QL::TableQL&&, QL::Creds, SL )ε->jobject override{ ASSERT(false); return {}; }
	};
}
#pragma once
#include <jde/app/AppQL.h>

namespace Jde::Opc::Server{
	struct OpcQL;
	α QLPtr()ι->sp<OpcQL>;
	α QL()ι->OpcQL&;
	α ConfigureQL( sp<DB::AppSchema> schema, sp<Access::Authorize> authorizer )ι->void;
	α Schemas()ι->const vector<sp<DB::AppSchema>>&;

	struct OpcQL : App::AppQL{
		OpcQL( sp<DB::AppSchema>&& schema, sp<Access::Authorize> authorizer )ι;
		virtual ~OpcQL(){};
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override;
	};
}
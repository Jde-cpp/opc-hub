#pragma once
#include <jde/app/AppQL.h>

namespace Jde::Opc::Server{
	struct OpcQL;
	α QLPtr()ι->sp<OpcQL>;
	α QL()ι->OpcQL&;
	α ConfigureQL( sp<DB::AppSchema> schema, sp<Access::Authorize> authorizer, string logSchema )ι->void;//logSchema:  this instance's `opc.<instance>`.
	α Schemas()ι->const vector<sp<DB::AppSchema>>&;

	struct OpcQL : App::AppQL{
		OpcQL( sp<DB::AppSchema>&& schema, sp<Access::Authorize> authorizer, string logSchema )ι;
		virtual ~OpcQL(){};
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override;
	};
}
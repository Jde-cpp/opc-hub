#pragma once
#include <jde/ql/types/MutationQL.h>

namespace Jde::DB{ struct AppSchema; }
namespace Jde::App::Server{
	struct InstanceTagLevelAwait final : TAwaitEx<jvalue, TAwait<vector<DB::Row>>::Task>{
		using base = TAwaitEx<jvalue, TAwait<vector<DB::Row>>::Task>;
		InstanceTagLevelAwait( QL::TableQL&& q, UserPK executer, SRCE )ι:base{sl}, _query{move(q)}, _executer{executer}{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName.starts_with("instanceTagLevel"); }
	private:
		α Execute()ι->TAwait<vector<DB::Row>>::Task override;
		QL::TableQL _query;
		UserPK _executer;//the `running` column asks the instance itself, and its logSetting query wants an authenticated executer.
	};

	struct InstanceTagLevelMAwait final : TAwaitEx<jvalue, TAwait<uint32>::Task>{
		using base = TAwaitEx<jvalue, TAwait<uint32>::Task>;
		InstanceTagLevelMAwait( QL::MutationQL&& m, UserPK executer, SRCE )ι:base{sl}, _mutation{move(m)}, _executer{executer}{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool{ return m.CommandName=="updateInstanceTagLevel"; }
	private:
		α Execute()ι->TAwait<uint32>::Task override;
		QL::MutationQL _mutation;
		UserPK _executer;
	};
}

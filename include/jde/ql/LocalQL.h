#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/ql.h>
#include <jde/ql/IQL.h>
#include <jde/ql/QLAwait.h>

namespace Jde::DB{ struct AppSchema; struct IDataSource; }
namespace Jde::QL{
	struct IListener;
	struct LocalQL /*abstract*/ : IQL{
		LocalQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer )ι: _authorizer{authorizer}, _schemas{move(schemas)}{}
		α Authorizer()ε->Access::Authorize& override{ return *_authorizer; }
		α AuthorizerPtr()ε->sp<Access::Authorize> override{ return _authorizer; }
		α Query( string query, jobject vars, UserPK executer, bool returnRaw=true, SRCE )ε->up<TAwait<jvalue>> override{ return mu<QLAwait<jvalue>>( move(query), vars, executer, shared_from_this(), returnRaw, sl ); }
		α QueryObject( string query, jobject vars, UserPK executer, bool returnRaw=true, SRCE )ε->up<TAwait<jobject>> override{ return mu<QLAwait<jobject>>( move(query), vars, executer, shared_from_this(), returnRaw, sl ); }
		α QueryArray( string query, jobject vars, UserPK executer, bool returnRaw=true, SRCE )ε->up<TAwait<jarray>> override{ return mu<QLAwait<jarray>>( move(query), vars, executer, shared_from_this(), returnRaw, sl ); }
		α Upsert( string query, jobject vars, UserPK executer )ε->jarray override;
		α Subscribe( string&& query, jobject vars, sp<IListener> listener, UserPK executer, SRCE )ε->up<TAwait<vector<SubscriptionId>>> override;

		α DS()ι->DB::IDataSource&;
		α GetTable( str tableName, SRCE )ε->DB::Table&;
		α GetTablePtr( str tableName, SRCE )ε->sp<DB::Table>;
		α Schemas()Ι->const vector<sp<DB::AppSchema>>& override{ return _schemas; }

		template<class T=jobject> α QuerySync( string query, jobject vars, UserPK executer, bool returnRaw=true, SRCE )ε->T;
		private:
			sp<Access::Authorize> _authorizer;
			vector<sp<DB::AppSchema>> _schemas;
	};

	Ŧ LocalQL::QuerySync( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->T{
		return BlockAwait( QL::QLAwait<T>{move(query), vars, executer, shared_from_this(), returnRaw, sl} );
	}
}
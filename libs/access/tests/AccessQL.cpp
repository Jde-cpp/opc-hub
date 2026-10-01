#include "AccessQL.h"
#include <jde/access/server/accessServer.h>

namespace Jde::Access::Tests{
	AccessQL::AccessQL( vector<sp<DB::AppSchema>>&& schemas, sp<Access::Authorize> authorizer )ι:
		QL::LocalQL{ vector<sp<DB::AppSchema>>{schemas}, authorizer }{
		QL::Configure( move(schemas) );
	}

	α AccessQL::CustomQuery( QL::TableQL& q, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>>{
		return Server::CustomQuery( q, executer, sl );
	}
	α AccessQL::CustomMutation( QL::MutationQL& m, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>>{
		return Server::CustomMutation( m, executer, sl );
	}
}
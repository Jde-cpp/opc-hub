#include "AppServerQL.h"
#include <jde/access/server/accessServer.h>
#include <jde/app/log/LogQLAwait.h>
#include <jde/app/IApp.h>
#include "../LocalClient.h"
#include "AppQLAwait.h"
#include "InstanceTagLevelAwait.h"

namespace Jde::App{
	sp<QL::LocalQL> _ql;
	α Server::QLPtr()ι->sp<QL::LocalQL>{ return App::_ql; }
	α Server::QL()ι->QL::LocalQL&{ return *QLPtr(); }
	α Server::ConfigureQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer )ι->void{
		QL::Configure( schemas );
		_ql = ms<AppServerQL>( move(schemas), move(authorizer) );
	}
	α Server::SetQL( sp<QL::LocalQL> ql )ι->void{ _ql = move(ql); }
}

namespace Jde::App::Server{
	AppServerQL::AppServerQL( vector<sp<DB::AppSchema>>&& schemas, sp<Access::Authorize>&& authorizer )ι:
		AppQL{ move(schemas), move(authorizer) }
	{}

	α AppServerQL::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = Access::Server::CustomQuery(q, creds, sl); await )
			return await;
		return AppQLAwait::Test( q, creds, sl );
	}
	α AppServerQL::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = LogSettingsMutation(m, creds, AppClient(), sl); await )
			return await;
		if( auto await = Access::Server::CustomMutation(m, creds, sl); await )
			return await;
		if( auto await = InstanceTagLevelMAwait::IsApplicable(m) ? mu<InstanceTagLevelMAwait>(move(m), creds.UserPK(), sl) : nullptr; await )
			return await;
		return nullptr;
	}
}
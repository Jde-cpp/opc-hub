#include "AppServerQL.h"
#include <jde/access/server/accessServer.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/app/log/LogQLAwait.h>
#include <jde/app/IApp.h>
#include <jde/web/server/SessionGraphQL.h>
#include <jde/web/server/SettingQL.h>
#include "../LocalClient.h"
#include "../appStartup.h"
#include "ConnectionQLAwait.h"
#include "InstanceSessionsAwait.h"
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

	Ω sessionGraphQL()ι->Web::Server::SessionGraphQL{ return { Server::AppClient(), Server::Authorizer(), Server::AppSchema()->Name }; }
	α Server::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = Access::Server::CustomQuery(q, creds, sl); await )
			return await;
		if( ConnectionQLAwait::IsApplicable(q) )
			return mu<ConnectionQLAwait>( move(q), move(creds), sl );
		if( q.JsonName.starts_with("setting") )
			return Web::Server::SettingQL( move(q), AppClient(), sl );
		if( InstanceTagLevelAwait::IsApplicable(q) )
			return mu<InstanceTagLevelAwait>( move(q), creds.UserPK(), sl );
		if( InstanceSessionsAwait::IsApplicable(q) )
			return mu<InstanceSessionsAwait>( move(q), creds.UserPK(), sl );
		if( Web::Server::SessionGraphQL::IsApplicable(q) )//this process's own map - the hub's too, which asks only this.
			return sessionGraphQL().Select( q, creds.UserPK(), sl );
		return nullptr;
	}
	α Server::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = Access::Server::CustomMutation(m, creds, sl); await )
			return await;
		if( InstanceTagLevelMAwait::IsApplicable(m) )
			return mu<InstanceTagLevelMAwait>( move(m), creds.UserPK(), sl );
		if( Web::Server::SessionGraphQL::IsPurge(m) )//a stand-alone gateway's logout, as its own account.
			return sessionGraphQL().Purge( m, creds.UserPK(), sl );
		return nullptr;
	}
}

namespace Jde::App::Server{
	AppServerQL::AppServerQL( vector<sp<DB::AppSchema>>&& schemas, sp<Access::Authorize>&& authorizer )ι:
		AppQL{ move(schemas), move(authorizer), App::Server::AppSchema()->Name, App::HiddenLogInstances }//as logGate and InstanceLogSchema:  the app schema's own name.
	{}

	α AppServerQL::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		return Server::CustomQuery( q, move(creds), sl );
	}
	α AppServerQL::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = LogSettingsMutation(m, creds, AppClient(), sl); await )
			return await;
		return Server::CustomMutation( m, move(creds), sl );
	}
}
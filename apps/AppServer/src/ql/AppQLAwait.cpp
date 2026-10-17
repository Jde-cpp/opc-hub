#include "AppQLAwait.h"
#include <jde/ql/QLAwait.h>
#include <jde/access/server/accessServer.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/web/server/SessionGraphQL.h>
#include <jde/web/server/SettingQL.h>
#include "../LocalClient.h"
#include "../appStartup.h"
#include "ConnectionQLAwait.h"
#include "InstanceSessionsAwait.h"
#include "InstanceTagLevelAwait.h"
#define let const auto

namespace Jde::App::Server{
	α AppQLAwait::Test( QL::TableQL& q, QL::Creds& executer, SL sl )->up<TAwait<jvalue>>{
		up<TAwait<jvalue>> y;
		if( auto await = Access::Server::CustomQuery( q, executer, sl ); await )
			y = move(await);
		else if( ConnectionQLAwait::IsApplicable(q) )
			y = mu<ConnectionQLAwait>( move(q), move(executer), sl );
		else if( q.JsonName.starts_with( "setting" ) )
			y = Web::Server::SettingQL( move(q), AppClient(), sl );
		else if( InstanceTagLevelAwait::IsApplicable(q) )
			y = mu<InstanceTagLevelAwait>( move(q), executer.UserPK(), sl );
		else if( InstanceSessionsAwait::IsApplicable(q) )
			y = mu<InstanceSessionsAwait>( move(q), executer.UserPK(), sl );
		else if( Web::Server::SessionGraphQL::IsApplicable(q) )//this process's own map - the hub's too, which asks only this.
			y = Web::Server::SessionGraphQL{ AppClient(), Authorizer(), AppSchema()->Name }.Select( q, executer.UserPK(), sl );
		return y;
	}
}
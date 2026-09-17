#include "AppQLAwait.h"
#include <jde/ql/QLAwait.h>
#include <jde/access/server/accessServer.h>
#include <jde/web/server/SettingQL.h>
#include "../LocalClient.h"
#include "ConnectionQLAwait.h"
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
			y = mu<Web::Server::SettingQLAwait>( move(q), AppClient(), sl );
		else if( InstanceTagLevelAwait::IsApplicable(q) )
			y = mu<InstanceTagLevelAwait>( move(q), executer.UserPK(), sl );
		return y;
	}
}
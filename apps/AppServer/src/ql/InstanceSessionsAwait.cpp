#include "InstanceSessionsAwait.h"
#include <jde/access/Authorize.h>
#include <jde/app/usings.h>
#include "../LocalClient.h"
#include "../appStartup.h"
#include "../ServerSocketSession.h"
#include "../WebServer.h"

#define let const auto
namespace Jde::App::Server{
	α InstanceSessionsAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			_query.Args = _query.ExtrapolateVariables();//bound, so the text sent on carries the values and not the names.
			let instanceId = Json::AsNumber<ProgInstPK>( _query.Args, "instanceId" );
			//The hook's gate, asked here as well:  the local leg runs as the system user, and the remote one as whoever this process
			//vouches for - neither may pass what the caller would not.
			Authorizer()->TestAdminSlug( "sessions", _executer, _sl );
			_query.Args.erase( "instanceId" );//the instance's hook lists its own map; it knows no instances.
			_query.ReturnRaw = true;//the bare list - TablesAwait files it under `sessions` for the caller.
			if( instanceId==AppClient()->InstancePK() )
				Resume( co_await *AppClient()->Query<jvalue>(_query.ToString(), {}, true, _sl) );
			else if( auto session = FindInstance(instanceId); session ){
				IWebsocketSession& base = *session;//ServerSocketSession's own QueryClient override hides the awaitable-returning overloads.
				Resume( co_await base.QueryClient(move(_query), _executer, _sl) );
			}
			else
				throw Exception{ _sl, {ELogTags::Sessions, EHttpStatus::NotFound}, "[{}]The instance is not connected, so its web sessions can not be listed.", instanceId };
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
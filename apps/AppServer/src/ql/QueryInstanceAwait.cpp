#include "QueryInstanceAwait.h"
#include "../LocalClient.h"
#include "../ServerSocketSession.h"
#include "../WebServer.h"

namespace Jde::App::Server{
	α QueryInstanceAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			if( _instanceId==AppClient()->InstancePK() )
				Resume( co_await *AppClient()->Query<jvalue>(_ql.ToString(), {}, true, _sl) );
			else if( auto session = FindInstance(_instanceId); session ){
				IWebsocketSession& base = *session;//ServerSocketSession's own QueryClient override hides the awaitable-returning overloads.
				Resume( co_await base.QueryClient(move(_ql), _executer, _sl) );
			}
			else
				Resume( nullopt );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
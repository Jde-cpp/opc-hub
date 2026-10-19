#include "QueryInstanceAwait.h"
#include "../LocalClient.h"
#include "../ServerSocketSession.h"
#include "../WebServer.h"

namespace Jde::App::Server{
	α QueryInstanceAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			if( _instanceId==AppClient()->InstancePK() )//this process:  there is no socket back to it.
				Resume( co_await *AppClient()->Query<jvalue>(_ql.ToString(), {}, true, _sl) );
			else if( auto session = FindInstance(_instanceId); session )
				Resume( co_await session->QueryClient(move(_ql), _executer, _sl) );
			else
				Resume( nullopt );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
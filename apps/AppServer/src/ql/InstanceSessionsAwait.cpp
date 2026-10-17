#include "InstanceSessionsAwait.h"
#include <jde/access/Authorize.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/app/usings.h>
#include "../LocalClient.h"
#include "../appStartup.h"
#include "QueryInstanceAwait.h"

#define let const auto
namespace Jde::App::Server{
	α InstanceSessionsAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			_query.Args = _query.ExtrapolateVariables();//bound, so the text sent on carries the values and not the names.
			let instanceId = Json::AsNumber<ProgInstPK>( _query.Args, "instanceId" );
			//SessionGraphQL's gate, asked here:  the local leg runs as the system user, and an instance answers ungated (Forwarded) -
			//neither may pass what the caller would not.
			Authorizer()->Test( AppSchema()->Name, "sessions", Access::ERights::Read, _executer, _sl );
			_query.Args.erase( "instanceId" );//the instance lists its own map, and refuses an instanceId.
			_query.ReturnRaw = true;//the bare list - TablesAwait files it under `sessions` for the caller.
			if( auto y = co_await QueryInstanceAwait{instanceId, move(_query), _executer, _sl}; y )
				Resume( move(*y) );
			else
				throw Exception{ _sl, {ELogTags::Sessions, 0, EHttpStatus::NotFound}, "[{}]The instance is not connected, so its web sessions can not be listed.", instanceId };
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
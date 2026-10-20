#include "LocalClient.h"
#include <jde/access/Authorize.h>
#include <jde/app/log/ProtoLog.h>

namespace Jde::App{
	sp<Server::LocalClient> _appClient = ms<Server::LocalClient>();
	α Server::AppClient()ι->sp<LocalClient>{ return _appClient; }
	sp<Access::Authorize> _authorizer = ms<Access::Authorize>( "App" );
	α Server::Authorizer()ι->sp<Access::Authorize>{ return _authorizer; }

namespace Server{
	α LocalClient::InitLogging()ι->void{
		Logging::Add<ProtoLog>( "proto" );
		Logging::Init();
	}
}}
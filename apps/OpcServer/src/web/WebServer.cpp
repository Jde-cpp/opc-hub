#include "WebServer.h"
#include "../OpcServerAppClient.h"
#include <jde/web/server/Server.h>
#include "../ql/OpcQL.h"
#include "jde/fwk/process/process.h"
#include "jde/fwk/settings.h"

namespace Jde::Opc{
	sp<Server::RequestHandler> _requestHandler;
	α Server::StartWebServer( jobject&& settings )ε->void{
		_requestHandler = ms<RequestHandler>( move(settings), AppClient() );
		Web::Server::Start( _requestHandler );
		Process::AddShutdownFunction( [](bool terminate, SL sl ){StopWebServer(terminate, sl);} );//TODO move to Web::Server
	}
	α Server::StopWebServer( bool terminate, SL sl )ι->void{
		Web::Server::Stop( move(_requestHandler), terminate, sl );
	}
	namespace Server{
		α RequestHandler::Schemas()ι->const vector<sp<DB::AppSchema>>&{
			return Opc::Server::Schemas();
		}
	}
}
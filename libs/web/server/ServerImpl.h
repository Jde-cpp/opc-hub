#pragma once
#include "Streams.h"
#include <jde/web/server/HttpRequest.h>
#include <jde/web/server/Server.h>
#include <jde/web/server/usings.h>
#include <jde/app/IApp.h>
#include <jde/web/server/IRequestHandler.h>
namespace Jde::DB{ struct AppSchema; }
namespace Jde::Web::Server{
namespace Internal{
	α Start( sp<IRequestHandler> handler )ε->void;
	α Stop( sp<IRequestHandler>&& handler, bool terminate, SL sl )ι->void;
	α RunSocketSession( sp<IWebsocketSession>&& session, const IRequestHandler* handler )ι->void;//handler: the listener that accepted it - Stop closes only its own.
	α NextConnectionIndex()ι->uint32;//process-wide: socket ids key _socketSessions across every listener in the process.
	α RemoveSocketSession( SocketId id )ι->void;
	α CloseSocketSessions( SessionPK sessionId )ι->uint;
}

	α HandleRequest( HttpRequest req, sp<IRestStream> stream, IRequestHandler* reqHandler )ι->TAwait<sp<SessionInfo>>::Task;
	α SendOptions( const HttpRequest&& req )ι->http::message_generator;
	α SendServerSettings( HttpRequest req, sp<IRestStream> stream, sp<App::IApp> appServer )ι->TAwait<sp<SessionInfo>>::Task;
}


#include "WebServer.h"
#include <jde/web/server/IWebsocketSession.h>
#include "../../AppServer/src/ServerSocketSession.h"
#include "../../OpcGateway/src/GatewaySocketSession.h"
#include "../../OpcGateway/src/WebServer.h"
#include "HttpRequestAwait.h"
#define let const auto

namespace Jde::Opc::Hub{
	constexpr ELogTags _tags{ ELogTags::Socket | ELogTags::Server };
	α RequestHandler::HandleRequest( Web::Server::HttpRequest&& req, SL sl )ι->up<Web::Server::IHttpRequestAwait>{
		return mu<Hub::HttpRequestAwait>( move(req), sl );
	}
	α RequestHandler::WebsocketSession( sp<Web::Server::IRestStream>&& stream, beast::flat_buffer&& buffer, Web::Server::TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<Web::Server::IWebsocketSession>{
		sv target{ req.target() };
		if( let query = target.find('?'); query!=sv::npos )
			target = target.substr( 0, query );
		if( target.empty() || target=="/" )
			return App::Server::RequestHandler::WebsocketSession( move(stream), move(buffer), move(req), move(userEndpoint), connectionIndex );
		if( target==OpcSocketPath )
			return Gateway::Server::Accept( move(stream), move(buffer), move(req), move(userEndpoint), connectionIndex );
		WARN( "[{}]websocket upgrade on '{}' - not a protocol path ('/' app, '{}' opc); declined.", connectionIndex, target, OpcSocketPath );
		return nullptr;
	}
}
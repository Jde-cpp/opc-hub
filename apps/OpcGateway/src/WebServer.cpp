#include "WebServer.h"
#include <jde/web/server/Server.h>
#include <jde/app/client/IAppClient.h>
#include "GatewaySocketSession.h"
#include "HttpRequestAwait.h"
#include "GatewayAppClient.h"
#include "ql/GatewayQL.h"

namespace Jde::Opc{
	static concurrent_flat_map<uint,sp<Opc::Gateway::GatewaySocketSession>> _sessions;
	static sp<Gateway::RequestHandler> _requestHandler;
	α Gateway::StartWebServer( jobject&& settings )ε->void{
		_requestHandler = ms<RequestHandler>( move(settings), AppClient(), Gateway::QLPtr() );
		Web::Server::Start( _requestHandler );
		Process::AddShutdownFunction( [](bool terminate, SL sl ){
			Server::Shutdown( terminate, sl );
			StopWebServer(terminate, sl);
		});//TODO move to Web::Server
	}

	α Gateway::StopWebServer( bool terminate, SL sl )ι->void{
		Web::Server::Stop( move(_requestHandler), terminate, sl );
	}
namespace Gateway{
	α Server::Accept( sp<Web::Server::IRestStream>&& stream, beast::flat_buffer&& buffer, Web::Server::TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<GatewaySocketSession>{
		auto session = ms<GatewaySocketSession>( move(stream), move(buffer), move(req), move(userEndpoint), connectionIndex );
		_sessions.emplace( session->Id(), session );
		return session;
	}
	α Server::RemoveSession( uint socketSessionId )ι->void{
		_sessions.erase( socketSessionId );
	}
	α Server::Shutdown( bool, SL )ι->void{
		_sessions.erase_if( []( auto& s ){
			s.second->Close();
			return true;
		});
	}

	using namespace Jde::Web::Server;
	α RequestHandler::HandleRequest( HttpRequest&& req, SL sl )ι->up<IHttpRequestAwait>{
		return mu<HttpRequestAwait>( move(req), sl );
	}

	α RequestHandler::WebsocketSession( sp<Web::Server::IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<Web::Server::IWebsocketSession>{
		return Server::Accept( move(stream), move(buffer), move(req), move(userEndpoint), connectionIndex );
	}
	α RequestHandler::Schemas()ι->const vector<sp<DB::AppSchema>>&{ return Gateway::Schemas(); }
}}
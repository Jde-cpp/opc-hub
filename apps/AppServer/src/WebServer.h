#pragma once
#include <jde/web/server/IRequestHandler.h>
#include <jde/web/Jwt.h>
#include <jde/web/server/Server.h>
#include "ql/QuerySessionsAwait.h"
#include "HttpRequestAwait.h"


namespace Jde::DB{ struct AppSchema; }
namespace Jde::QL{ struct LocalQL; }
namespace Jde::Web::Server{ struct IRestStream; }
namespace Jde::App::Server{
	using namespace Jde::Web::Server;
	struct ServerSocketSession; struct RequestHandler;

	α GetAppPK()ι->ProgramPK;
	α SetAppPKs( std::tuple<ProgramPK, ProgInstPK, ConnectionPK> x )ι->void;
	α Schemas()ι->const vector<sp<DB::AppSchema>>&;
	α GetJwt( UserPK userPK, string name, string slug, string endpoint, SessionPK sessionId, TimePoint expires, string description )ε->Web::Jwt;
	α RemoveExisting( str host, PortType port )ι->void;
	α GetRequestHandler()ι->sp<RequestHandler>;
	α StartWebServer( jobject&& settings )ε->void;
	α StartWebServer( sp<RequestHandler> handler )ε->void;//a host's own handler (OpcHub: routes both apps from one listener) - it must be an App::Server::RequestHandler, GetJwt signs through it.
	α StopWebServer( bool terminate, SL sl )ι->void;
	α AddSocketSession( sp<ServerSocketSession> session )ι->void;//the app-protocol registry a handler adds its accepted sockets to.

	α BroadcastAppStatus()ι->void;
	α FindApplications( str name )ι->vector<Proto::FromClient::Instance>;
	//An app role hosted in this process (OpcHub's gateway) registers here instead of over a socket, so /opcGateways and
	//FindApplications serve it beside the remote registrations.  Listed first: the SPA's password login uses gateways[0].
	α AddLocalInstance( Proto::FromClient::Instance instance )ι->void;
	α FindApp( ProgramPK appPK, optional<ProgInstPK> instancePK )ε->sp<ServerSocketSession>;
	α FindConnection( ConnectionPK connectionPK )ι->sp<ServerSocketSession>;
	α FindInstance( ProgInstPK instancePK )ι->sp<ServerSocketSession>;
	α OnSessionDisconnect( sp<ServerSocketSession> session )ι->void;

	α QuerySessions( QL::TableQL ql, UserPK executer, SRCE )ι->QuerySessionsAwait;

	α UnsubscribeLogs( ProgInstPK instancePK )ι->bool;
	α Write( ProgramPK appPK, optional<ProgInstPK> instancePK, Proto::FromServer::Transmission&& msg )ε->ConnectionPK;

	struct RequestHandler : IRequestHandler{
		RequestHandler( jobject&& settings )ι;
		α HandleRequest( HttpRequest&& req, SRCE )ι->up<IHttpRequestAwait> override{ return mu<HttpRequestAwait>( move(req), sl ); }
		α Jwt( UserPK userPK, string&& name, string&& slug, string&& endpoint, SessionPK sessionId, TimePoint expires, string&& description )ε->Web::Jwt;
		α Schemas()ι->const vector<sp<DB::AppSchema>>& override{ return Server::Schemas(); }
		α WebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<IWebsocketSession> override;
		α QLServer()ι->sp<QL::IQL> override;
	};
}
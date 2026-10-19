#pragma once
#include <jde/web/server/IRequestHandler.h>
#include <jde/web/Jwt.h>
#include <jde/web/server/Server.h>
#include "ql/QuerySessionsAwait.h"

namespace Jde::DB{ struct AppSchema; }
namespace Jde::App{ struct ConnectionPks; }
namespace Jde::QL{ struct LocalQL; }
namespace Jde::Web::Server{ struct IRestStream; }
namespace Jde::App::Server{
	using namespace Jde::Web::Server;
	struct ServerSocketSession; struct RequestHandler;

	α SetAppPKs( const ConnectionPks& pks )ι->void;
	α Schemas()ι->const vector<sp<DB::AppSchema>>&;
	α MintJwt( UserPK userPK, string name, string slug, string endpoint, SessionPK sessionId, TimePoint expires, string description )ε->Web::Jwt;
	α RemoveExisting( str host, PortType port )ι->void;
	//A socket's hold on its registration's (application, instance name), from ClaimInstance until the guard ends - after the
	//registration is published on the session, so a concurrent ClaimInstance always sees one or the other.
	struct InstanceClaim final{
		InstanceClaim( SocketId socketId )ι:Socket{ socketId }{}
		InstanceClaim( InstanceClaim&& x )ι:Socket{ std::exchange(x.Socket, 0) }{}
		~InstanceClaim();
		SocketId Socket;
	};
	//Throws when another socket, live or mid-registration, holds `instance`'s (application, instance name) with a different
	//auth_resource:  both would share one instance row, and the last to register would decide whose `logs` governs the other's
	//lines (325-logs-resource review #4).  No conflict:  the same process (host and pid) registering a second role, or a socket
	//on the same host:port - a restart, which RemoveExisting supersedes.
	α ClaimInstance( SocketId socketId, const Proto::FromClient::Instance& instance )ε->InstanceClaim;
	α StartWebServer( jobject&& settings )ε->void;
	α StartWebServer( sp<RequestHandler> handler )ε->void;//a host's own handler (OpcHub: routes both apps from one listener) - it must be an App::Server::RequestHandler;  MintJwt signs with its settings' private key.
	α StopWebServer( bool terminate, SL sl )ι->void;
	α AddSocketSession( sp<ServerSocketSession> session )ι->void;//the app-protocol registry a handler adds its accepted sockets to.

	α FindApplications( str name )ι->vector<Proto::FromClient::Instance>;
	//An app role hosted in this process (OpcHub's gateway) registers here instead of over a socket, so /opcGateways and
	//FindApplications serve it beside the remote registrations.  Listed first: the SPA's password login uses gateways[0].
	α AddLocalInstance( Proto::FromClient::Instance instance )ι->void;
	α FindApp( ProgramPK appPK, optional<ProgInstPK> instancePK )ε->sp<ServerSocketSession>;
	α FindConnection( ConnectionPK connectionPK )ι->sp<ServerSocketSession>;
	α FindInstance( ProgInstPK instancePK )ι->sp<ServerSocketSession>;
	α OnSessionDisconnect( sp<ServerSocketSession> session )ι->void;

	α QuerySessions( QL::TableQL ql, UserPK executer, SRCE )ι->QuerySessionsAwait;


	struct RequestHandler : IRequestHandler{
		RequestHandler( jobject&& settings )ι;
		α HandleRequest( HttpRequest&& req, SRCE )ι->up<IHttpRequestAwait> override;
		α Schemas()ι->const vector<sp<DB::AppSchema>>& override{ return Server::Schemas(); }
		α WebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<IWebsocketSession> override;
		α QLServer()ι->sp<QL::IQL> override;
	};
}
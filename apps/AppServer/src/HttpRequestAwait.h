#pragma once
#include <jde/web/server/IHttpRequestAwait.h>

namespace Jde::App::Server{
	//The AppServer's routes as free functions, so a host that routes both apps from one listener (OpcHub) serves them beside
	//the gateway's.  Ready answers without suspending; the suspending ones complete the await's handle, as HttpRequestAwait does.
	namespace Routes{
		α Ready( Web::Server::HttpRequest& req )ι->up<jvalue>;//GET /GoogleAuthClientId, /opcGateways, /opcServers - null for any other request.
		α LoginJwt( Web::Server::HttpRequest&& req, Web::Server::IHttpRequestAwait::Handle h )ι->void;//POST /login with a Bearer JWT - the session was minted by Server::HandleRequest.
		α Logout( Web::Server::HttpRequest&& req, Web::Server::IHttpRequestAwait::Handle h )ι->void;//POST /logout - {removed}.
	}
	struct HttpRequestAwait final: Web::Server::IHttpRequestAwait{
		using base = Web::Server::IHttpRequestAwait;
		HttpRequestAwait( Web::Server::HttpRequest&& req, SRCE )ι;
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
		α await_resume()ε->Web::Server::HttpTaskResult override;
	};
}

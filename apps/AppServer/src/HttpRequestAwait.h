#pragma once
#include <jde/web/server/IHttpRequestAwait.h>

namespace Jde::App::Server{
	//The AppServer's routes as free functions, so a host that routes both apps from one listener (OpcHub) serves them beside
	//the gateway's.  Ready ones return the json; the suspending ones complete the await's handle, as HttpRequestAwait does.
	namespace Routes{
		α GoogleAuthClientId()ι->jvalue;
		α Instances( bool opcServers, bool identified )ι->jvalue;//GET /opcGateways | /opcServers: what discovery needs anonymously, the rest to a caller with a user.
		α LoginJwt( Web::Server::HttpRequest& req, Web::Server::IHttpRequestAwait::Handle h )ι->void;//POST /login with a Bearer JWT - the session was minted by Server::HandleRequest.
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

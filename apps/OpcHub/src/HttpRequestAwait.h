#pragma once
#include "../../OpcGateway/src/HttpRequestAwait.h"

namespace Jde::Opc::Hub{
	using namespace Jde::Web::Server;
	//The two apps' REST routes from one listener - one explicit table rather than one await falling through to the other on 404:
	//an IHttpRequestAwait owns its request and reports a miss by moving it into a RestException, so chaining would mean a second
	//coroutine recovering the request from the exception.  Derives from the gateway's for its routes and await_resume's
	//UAClientException arm; the AppServer's routes come from App::Server::Routes.
	//  GET  /GoogleAuthClientId, /opcGateways, /opcServers   (app)      GET /ErrorCodes  (gateway)
	//  POST /login: `Authorization: Bearer <jwt>` is the app's (the session was minted by Server::HandleRequest); a JSON body
	//               with `opc` is the gateway's OPC user/password login; neither is 401 as the AppServer answers.
	//  POST /logout: the gateway's OPC credentials dropped, then the web session removed - one call closes both protocols'
	//               sockets (Sessions::Remove); no upstream purgeSession, which in-process would only re-enter that.
	//  ?opc=…: the gateway's CoHandleRequest.
	//  GET  anything else: the site (Web::Server::StaticSite, /http/site) - a file under it, or its index.html for a route of the
	//       page (install-issues #3/#4);  then the 404.
	struct HttpRequestAwait final : Gateway::HttpRequestAwait{
		using base = Gateway::HttpRequestAwait;
		HttpRequestAwait( HttpRequest&& req, SRCE )ι: base{ move(req), sl }{}
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
	private:
		α Schemas()Ι->const vector<sp<DB::AppSchema>>& override;
		α HubLogout()ι->void;
		α HasOpcBody()ι->bool;
		α ServeSite()ι->void;
	};
}

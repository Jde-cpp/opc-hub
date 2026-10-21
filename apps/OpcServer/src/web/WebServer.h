#pragma once
#include <jde/web/server/IRequestHandler.h>
#include <jde/web/server/IHttpRequestAwait.h>
#include <jde/app/client/IAppClient.h>
#include "../ql/OpcQL.h"

namespace Jde::Opc::Server{
	using namespace Jde::Web::Server;
	α StartWebServer( jobject&& settings )ε->void;
	α StopWebServer( bool terminate, SL sl )ι->void;
	struct RequestHandler final : IRequestHandler{
		RequestHandler( jobject settings, sp<App::Client::IAppClient> appServer )ι: IRequestHandler{ move(settings), move(appServer) }{}
		α HandleRequest( HttpRequest&& req, SRCE )ι->up<IHttpRequestAwait> override{ return mu<IHttpRequestAwait>( move(req), sl ); }//the 404:  /graphql is the listener's own.
		α QLServer()ι->sp<QL::IQL> override{ return QLPtr(); }
		α Schemas()ι->const vector<sp<DB::AppSchema>>& override;
	};
}
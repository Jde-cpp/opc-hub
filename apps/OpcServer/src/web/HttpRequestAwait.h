#pragma once
#include <jde/web/server/IHttpRequestAwait.h>
#include <jde/web/server/Sessions.h>

namespace Jde::Opc::Server{
	struct NodeId; struct Value;
	using namespace Jde::Web::Server;
	struct HttpRequestAwait final: IHttpRequestAwait{
		using base = IHttpRequestAwait;
		HttpRequestAwait( HttpRequest&& req, SRCE )ι;
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
		α await_resume()ε->HttpTaskResult override;
	};
}
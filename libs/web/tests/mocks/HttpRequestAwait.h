#pragma once
#include <jde/web/server/IHttpRequestAwait.h>
#include <thread>

namespace Jde::Web::Mock{
	using namespace Jde::Web::Server;
	struct HttpRequestAwait final: IHttpRequestAwait{
		using base = IHttpRequestAwait;
		HttpRequestAwait( HttpRequest&& req, SRCE )ι;
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
		α await_resume()ε->HttpTaskResult override;
	private:
		optional<HttpTaskResult> _result;
		optional<std::jthread> _thread;
	};
}
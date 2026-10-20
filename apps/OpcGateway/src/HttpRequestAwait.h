#pragma once
#include <jde/web/server/IHttpRequestAwait.h>

namespace Jde::Opc::Gateway{
	using namespace Jde::Web::Server;
	//Not final: a host routing both apps from one listener (OpcHub) derives from it for the OPC routes and await_resume's
	//UAClientException arm, and adds the AppServer's beside them.
	struct HttpRequestAwait : IHttpRequestAwait{
		using base = IHttpRequestAwait;
		HttpRequestAwait( HttpRequest&& req, SRCE )ι;
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
		α await_resume()ε->HttpTaskResult override;
	protected:
		α Login( str endpoint )ι->TAwait<optional<Web::FromServer::SessionInfo>>::Task;//POST /login {opc,user,password} - the OPC server's user/password.
		α Logout()ι->TAwait<jvalue>::Task;
	};
}
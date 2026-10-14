#pragma once
#include "HttpRequestAwait.h"
#include <jde/web/server/IRequestHandler.h>
#include <thread>

namespace Jde::Web::Server{ struct IWebsocketSession; }
namespace Jde::Web::Mock{
	using namespace Jde::Web::Server;
	const string Host{ "localhost" };
	constexpr PortType Port{ 5005 };
	α Start( jobject settings )ε->void;
	α Stop()ι->void;
	α AppClient()ι->sp<App::IApp>;
	//#4: a 3rd party (an AppServer) that is *not* local, so Sessions::FromSessionId takes the SessionInfoAwait fallback, and that
	//answers for a session minted at userEndpoint.  AppClient() can't drive it - its IsLocal() is true, which short-circuits the fallback.
	α ForeignAppClient( string userEndpoint, UserPK userPK={7} )ι->sp<App::IApp>;
	//#10: a 3rd party whose SessionInfoAwait *fails*, with a caller-chosen status - NotFound is "no such session", anything else is
	//"could not ask".  The gateway must cache anonymous only for the first.
	α FailingAppClient( EHttpStatus status )ι->sp<App::IApp>;
	α Gzip( sv text )ι->string;

	struct RequestHandler final : IRequestHandler{
		RequestHandler( jobject settings )ι: IRequestHandler{ settings, AppClient() }{}
		α HandleRequest( HttpRequest&& req, SRCE )ι->up<IHttpRequestAwait> override{ return mu<HttpRequestAwait>( move(req), sl ); }
		α QLServer()ι->sp<QL::IQL> override;
		α Schemas()ι->const vector<sp<DB::AppSchema>>& override{ return _schemas; }
		α WebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<Server::IWebsocketSession> override;

		α Query( QL::RequestQL&&, UserPK, bool, SL )ε->up<TAwait<jvalue>>{ ASSERT(false); return {}; }
	private:
		vector<sp<DB::AppSchema>> _schemas;
	};
}
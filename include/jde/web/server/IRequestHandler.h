#pragma once
#include "usings.h"
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/app/IApp.h>

namespace Jde::DB{ struct AppSchema; }
namespace Jde::QL{ struct IQL; }
namespace Jde::Web::Server{
	struct HttpRequest; struct IHttpRequestAwait; struct IWebsocketSession;struct IRestStream;
	struct IRequestHandler{
		IRequestHandler( jobject settings, sp<App::IApp> appServer )ι;
		virtual ~IRequestHandler()=default; //msvc error
		β HandleRequest( HttpRequest&& req, SRCE )ι->up<IHttpRequestAwait> =0;
		β QLServer()ι->sp<QL::IQL> =0;
		β Schemas()ι->const vector<sp<DB::AppSchema>>& =0;
		β WebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<IWebsocketSession> =0;

		α AppServer()ι->sp<App::IApp>{ return _appServer; }
		α CancelSignal()ι->sp<net::cancellation_signal>{ return _cancelSignal; }
		α Context()ι->ssl::context&{ return _ctx; }
		α SessionInfoAwait( SessionPK sessionPK, SL sl )ι->up<TAwait<Web::FromServer::SessionInfo>>{ return _appServer->SessionInfoAwait( sessionPK, sl ); }
		α Start()ι->void;
		α FailStart( string&& why )ι->void;
		α Stop( bool terminate, SL sl )ι->void;
		α BlockTillStarted()ε->void;
		α UserName( UserPK userPK )ι->string;

		struct WebServerSettings{
			WebServerSettings( jobject settings )ι:_crypto{Json::FindDefaultObject(settings, "ssl")}, _settings(move(settings)){}
			α Address()Ι->string{ return Json::FindString(_settings, "address" ).value_or( "0.0.0.0" ); }
			α Port()Ι->PortType{ return Json::FindNumber<PortType>(_settings, "port" ).value_or( 6809 ); }
			α Crypto()Ι->const Crypto::CryptoSettings&{ return _crypto; }
		private:
			Crypto::CryptoSettings _crypto;
			jobject _settings;
		};
		α Settings()Ι->const WebServerSettings&{ return _settings; }
	private:
		sp<App::IApp> _appServer;
		sp<net::cancellation_signal> _cancelSignal;
		ssl::context _ctx;
		WebServerSettings _settings;
		enum class EStartState : uint8{ None, Started, Failed };//a flag could not say "failed", which is why BlockTillStarted had no way out.
		atomic<EStartState> _started{ EStartState::None };
		string _startError;//written before the Failed store, read after the matching load - the atomic's release/acquire is what publishes it.
	};
}
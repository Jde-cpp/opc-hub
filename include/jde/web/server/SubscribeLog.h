#pragma once
#include <jde/app/usings.h>
#include "usings.h"

namespace Jde::QL{ struct Subscription; struct Filter; }
namespace Jde::Web::Server{
	struct IWebsocketSession;
	struct SubscribeLog final : Logging::ILogger, noncopyable{
		SubscribeLog( jobject settings, const uint32& appPK, const App::ConnectionPK& connectionPK )ε:ILogger{ move(settings) }, _appPK(appPK), _connectionPK(connectionPK){}
		Ω Unsubscribe( SocketId socketId )ι->void;
		α Shutdown( bool terminate, SL sl )ι->void override;
		α Add( sp<Web::Server::IWebsocketSession> session, vector<QL::Subscription>&& subs )ι->void;
		α Write( const Logging::Entry& m )ι->void override;
		α Write( const Logging::Entry& m, uint32 appPK, App::ConnectionPK connectionPK )ι->void override;
	private:
		uint32 _appPK;
		App::ConnectionPK _connectionPK;
	};
}
#pragma once
#include <jde/app/usings.h>
#include "usings.h"

namespace Jde::QL{ struct Subscription; struct Filter; }
namespace Jde::Web::Server{
	struct IWebsocketSession;
	struct SubscribeLog final : Logging::ILogger, noncopyable{
		//Asked once per Add, outside the logger:  throws when the executer may not subscribe, else returns the test each entry's
		//instance must pass.  That test runs inside the logger, so it may not log or take a lock anything logs under.
		using Gate = function<function<bool(App::ProgInstPK)>( UserPK executer )>;
		SubscribeLog( jobject settings, const uint32& appPK, const App::ConnectionPK& connectionPK, Gate gate={} )ε:ILogger{ move(settings) }, _appPK(appPK), _connectionPK(connectionPK), _gate{ move(gate) }{}
		Ω Unsubscribe( SocketId socketId )ι->void;
		α Shutdown( bool terminate, SL sl )ι->void override;
		α Add( sp<Web::Server::IWebsocketSession> session, vector<QL::Subscription>&& subs, UserPK executer )ε->void;
		α Write( const Logging::Entry& m )ι->void override;
		α Write( const Logging::Entry& m, uint32 appPK, App::ConnectionPK connectionPK )ι->void override;
	private:
		uint32 _appPK;
		App::ConnectionPK _connectionPK;
		Gate _gate;
	};
}
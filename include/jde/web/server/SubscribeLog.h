#pragma once
#include <condition_variable>
#include <thread>
#include <jde/app/usings.h>
#include "usings.h"

namespace Jde::QL{ struct Subscription; struct Filter; }
namespace Jde::Web::Server{
	struct IWebsocketSession; struct SessionSubscription;
	//Write only matches an entry against the subscriptions and queues it;  a thread of the logger's own runs each subscriber's
	//Gate test and writes to its session.  Write runs on whatever thread logged, maybe one holding a lock - the Authorize logs
	//under its Mutex - so nothing a delivery does can deadlock on a lock its logging thread holds (325-logs-resource review #8).
	struct SubscribeLog final : Logging::ILogger, noncopyable{
		//Asked once per Add, outside the logger:  throws when the executer may not subscribe, else returns the test each entry's
		//instance must pass.  That test runs on the delivery thread, so it may take a lock held while logging;  what it logs
		//reaches the other loggers but no subscriber.
		using Gate = function<function<bool(App::ProgInstPK)>( UserPK executer )>;
		SubscribeLog( jobject settings, const uint32& appPK, const App::ConnectionPK& connectionPK, Gate gate={} )ε;
		Ω Unsubscribe( SocketId socketId )ι->void;
		α Shutdown( bool terminate, SL sl )ι->void override;
		α Add( sp<Web::Server::IWebsocketSession> session, vector<QL::Subscription>&& subs, UserPK executer )ε->void;
		α Write( const Logging::Entry& m )ι->void override;
		α Write( const Logging::Entry& m, uint32 appPK, App::ConnectionPK connectionPK )ι->void override;
	private:
		struct Delivery{ Logging::Entry Entry; uint32 AppPK; App::ConnectionPK ConnectionPK; vector<sp<SessionSubscription>> Matches; };
		α Deliver( std::stop_token stop )ι->void;

		uint32 _appPK;
		App::ConnectionPK _connectionPK;
		Gate _gate;
		std::mutex _queueMutex;
		std::condition_variable_any _cv;
		vector<Delivery> _queue;
		std::jthread _thread;//last:  destroyed first, so the thread is joined before what it reads goes.
	};
}
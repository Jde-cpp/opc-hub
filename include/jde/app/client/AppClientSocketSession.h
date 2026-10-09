#pragma once
#include <jde/app/client/usings.h>
#include <absl/synchronization/mutex.h>
#include <jde/app/proto/app.FromClient.h>
#include <jde/access/usings.h>
#include <jde/ql/types/Subscription.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include <jde/app/proto/App.FromServer.pb.h>

namespace Jde::QL{ struct IListener; }
namespace Jde::App::Client{
	struct AppClientSocketSession;
	struct IAppClient;
	struct AppClientSocketSession final : Web::Client::TClientSocketSession<Jde::App::Proto::FromClient::Transmission,Jde::App::Proto::FromServer::Transmission>{
		Τ using await = Web::Client::ClientSocketAwait<T>;
		using base = Web::Client::TClientSocketSession<Proto::FromClient::Transmission,Proto::FromServer::Transmission>;
		AppClientSocketSession( sp<net::io_context> ioc, optional<ssl::context> ctx, sp<Access::Authorize> authorize, sp<IAppClient> appClient )ι;
		α AddSession( str domain, str loginName, Access::ProviderPK providerPK, str userEndPoint, bool isSocket, SRCE )ι->await<Web::FromServer::SessionInfo>;
		α Connect( SessionPK sessionId, SRCE )ι->await<Proto::FromServer::ConnectionInfo>;
		α Jwt( SRCE )ι->await<Web::Jwt>;
		α Login( Web::Jwt&& jwt, SRCE )ι->await<Web::FromServer::SessionInfo>;
		α SessionInfo( SessionPK creds, SRCE )ι->await<Web::FromServer::SessionInfo>;
		α Query( string&& q, jobject variables, bool returnRaw, SRCE )ι->await<jvalue> override;
		α Subscribe( string&& query, jobject variables, sp<QL::IListener> listener, SRCE )ε->await<jarray> override;
		α Unsubscribe( vector<QL::SubscriptionId>&& ids, SRCE )ι->void override;
		α QLServer()ι{ return _qlServer; }

		α ListenRemote( sp<QL::IListener> listener, QL::Subscription&& sub )ι->void;
		α StopListenRemote( sp<QL::IListener> listener, vector<QL::SubscriptionId> ids )ι->flat_set<QL::SubscriptionId>;//returns the ids removed - the caller unsubscribes server side, e.g. IQL::Unsubscribe.
		α ClearSubscriptions()ι->void;//what the close does: the ids died with the socket.  Remembered requests are untouched - that is what a reconnect replays.
		α OnSubscription( const jobject& m, QL::SubscriptionId clientId )ι->void;
	private:
		//Every request this session makes: a fresh id, logged as the first argument of the caller's message, then the await.
		template<class T, class... Args>
		α Request( ELogLevel level, ELogTags tags, SL sl, auto&& build, fmt::format_string<string const&, Args const&...>&& m, const Args&... args )ι->await<T>{
			const auto requestId = NextRequestId();
			LOGSL( level, sl, tags, move(m), hex(requestId), args... );
			return { build(requestId), requestId, shared_from_this(), sl };
		}
		α ListenersFor( QL::SubscriptionId id )Ι->flat_set<sp<QL::IListener>>;
		α ClientQuery( Proto::FromServer::ClientQuery proto, Jde::UserPK executer, RequestId requestId )ι->TAwait<jvalue>::Task;
		α Execute( string&& bytes, optional<Jde::UserPK> userPK, RequestId clientRequestId, uint8 depth )ι->void;
		α WriteException( runtime_error&&, RequestId requestId )ι->void;
		α WriteException( string&& e, RequestId requestId )ι->void;
		α ProcessTransmission( Proto::FromServer::Transmission&& t, optional<Jde::UserPK> userPK, optional<RequestId> clientRequestId, uint8 depth )ι->void;
		α Fail( Web::Client::PendingTask&& task, Exception&& e, RequestId requestId )ι->void;
		α OnRead( Proto::FromServer::Transmission&& transmission )ι->void override;
		α OnClose( beast::error_code ec )ι->void override;
		α CloseTasks( beast::error_code ec )ι->void override;
		α OnMessage( string&& j, RequestId requestId )ι->void;
		sp<IAppClient> _appClient;
		sp<Access::Authorize> _authorize;
		sp<QL::IQL> _qlServer;
		//Query/Variables are carried through to the ack so Subscriptions::Remember records what actually succeeded, rather
		//than what was merely attempted - a request the server rejects must not be replayed on every reconnect forever.
		struct SubscriptionRequest final{ sp<QL::IListener> Listener; vector<QL::Subscription> Subscriptions; string Query; jobject Variables; };
		concurrent_flat_map<RequestId, SubscriptionRequest> _subscriptionRequests;
		mutable absl::Mutex _subsMutex;
		flat_map<QL::SubscriptionId,flat_set<sp<QL::IListener>>> _subs ABSL_GUARDED_BY(_subsMutex);
#ifdef TESTS
	public:
		α ProcessTransmissionTest( Proto::FromServer::Transmission&& t, optional<Jde::UserPK> userPK, optional<RequestId> clientRequestId, uint8 depth )ι->void{ ProcessTransmission( move(t), userPK, clientRequestId, depth ); }
#endif
	};
}

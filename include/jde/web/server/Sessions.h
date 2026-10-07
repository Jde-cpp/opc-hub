#pragma once
#include <jde/db/usings.h>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/co/Task.h>
#include <jde/ql/IQLSession.h>

namespace Jde::App{ struct IApp; }
namespace Jde::Web{ struct Jwt; }
namespace Jde::Web::FromServer{ struct SessionInfo; }
//Holds web session information.
namespace Jde::Web::Server{
	struct SessionInfo;
	namespace Sessions::Internal{ α CreateSession( UserPK userPK, str endpoint, bool isSocket, bool add )ι->sp<SessionInfo>; }
	struct SessionInfo final : QL::IQLSession{
		SessionInfo()ι=default;
		SessionInfo( SessionPK sessionPK, steady_clock::time_point expiration, Jde::UserPK userPK, str userEndpointAddress, bool hasSocket )ι;

		SessionPK SessionId;
		string UserEndpoint;
		bool HasSocket;//before Expiration
		steady_clock::time_point Expiration;
		steady_clock::time_point LastServerUpdate;
		bool IsInitialRequest{};
		α NewExpiration()Ι->steady_clock::time_point;
	private:
		SessionInfo( SessionPK sessionPK, str UserEndpoint, bool hasSocket )ι;
		friend α Sessions::Internal::CreateSession( Jde::UserPK, str, bool, bool )ι->sp<SessionInfo>;
	};

	//TODO - change to GraphQL.
	namespace Sessions{
		α Add( UserPK userPK, string&& endpoint, bool isSocket )ι->sp<SessionInfo>;
		α Find( SessionPK sessionId )ι->sp<SessionInfo>;
		//Find, and slide a still-live session's expiration to NewExpiration() - a day for a socket-backed one.  No endpoint
		//check:  the caller vouches for the asker (the AppServer's SessionInfo handler, for a registered app instance).  An
		//expired session is returned as-is, never revived - the same rule as UpdateExpiration.
		α Extend( SessionPK sessionId )ι->sp<SessionInfo>;
		α Remove( SessionPK sessionId )ι->bool;
		α RestSessionTimeout()ι->steady_clock::duration;
		α Get()ι->vector<sp<SessionInfo>>;

		struct UpsertAwait : TAwait<sp<SessionInfo>>, noncopyable{
			using base = TAwait<sp<SessionInfo>>;
			UpsertAwait( str authorization, str endpoint, bool socket, sp<App::IApp> appClient, bool throw_=true, SRCE )ι:base{sl}, _appClient{move(appClient)}, _authorization{authorization}, _endpoint{endpoint}, _socket{socket}, _throw{throw_}{}
			α Suspend()ι->void override{ Run(); }
		private:
			α Run()ι->VoidTask;
			sp<App::IApp> _appClient; string _authorization; string _endpoint; bool _socket; bool _throw;
		};
	}
}

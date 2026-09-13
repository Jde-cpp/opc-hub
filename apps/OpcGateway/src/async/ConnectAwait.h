#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/web/server/Sessions.h>
#include "../types/ServerCnnctn.h"
#include "../auth/OpcServerSession.h"

namespace Jde::Opc::Gateway{
	struct UAClient; struct UAClientException;
	//The credential a web session's connect uses for `opc`:  the one stored by a password login, else the session's jwt,
	//else none (anonymous).  ConnectAwait keys the client on it - SearchQLAwait looks the same client up without connecting.
	α SessionCredential( SessionPK sessionId, UserPK user, str opc )ι->optional<Credential>;

	struct ConnectAwait final : TAwait<sp<UAClient>>, noncopyable{
		using base = TAwait<sp<UAClient>>;
		ConnectAwait( ServerCnnctnNK&& opcSlug, Credential cred, SRCE )ι:base{sl},_opcSlug{move(opcSlug)}, _cred{move(cred)}{}
		ConnectAwait( ServerCnnctnNK opc, SessionPK sessionId, UserPK user, SRCE )ι;
		ConnectAwait( ServerCnnctnNK opc, const Web::Server::SessionInfo& session, SRCE )ι;
		α Suspend()ι->void override;
		α await_resume()ε->sp<UAClient> override;
		Ω Resume( sp<UAClient> client )ι->void;
		Ω Resume( str slug, Credential cred, const UAClientException&& e )ι->void;
	private:
		Ω Resume( str slug, Credential cred, function<void(ConnectAwait::Handle)> resume )ι->void;
		α ResolveDefault()ι->TAwait<vector<ServerCnnctn>>::Task;//"" -> the default connection's slug, then Start.
		α Start()ι->void;//find a live client, else register and Create - Suspend once the slug is known.
		α Create()ι->TAwait<vector<ServerCnnctn>>::Task;
		Ω EraseRequests( str opcNK, Credential cred, lg& _ )ι->vector<ConnectAwait::Handle>;
		string _opcSlug;
		Credential _cred;
		SessionPK _sessionId{};//non-zero only on the web-session ctors: a successful connect is recorded in _sessions so opcSessions counts it.
		sp<UAClient> _result;
	};
}
#pragma once
#include <jde/ql/ql.h>
#include <jde/ql/QLAwait.h>
#include <jde/access/IAcl.h>
#include <jde/web/client/usings.h>
#include <jde/web/server/IWebsocketSession.h>
#include <jde/web/server/Sessions.h>
#include "appStartup.h"
#include "awaits/AddSessionAwait.h"
#include "awaits/ForwardExecutionAwait.h"

namespace Jde::App::Server{
	using namespace Jde::Web::Server;

	struct ServerSocketSession final: TWebsocketSession<Proto::FromServer::Transmission,Proto::FromClient::Transmission>, Access::IAdminAcl{
		using base = TWebsocketSession<Proto::FromServer::Transmission,Proto::FromClient::Transmission>;
		ServerSocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι;
		//The kInstance registration's pks - one value, not three members.  A _sessions visitor that matched a session on Program
		//and then read Connection could catch AddInstance between two plain stores and bind a forward to connection 0:  a key no
		//reply names and OnCloseConnection's sweep skips (appserver-review3 #14).
		α Pks()Ι->App::ConnectionPks{ lg _{_registrationMutex}; return _pks; }//prefer this to the three below when more than one is used for the same decision.
		α ProgramPK()Ι->ProgramPK{ return Pks().Program; }
		α Instance()Ι->Proto::FromClient::Instance{ lg _{_registrationMutex}; return _instance; }//copy under the lock: _instance is written on this session's strand (AddInstance) while _sessions visitors read it from other threads.
		α InstancePK()Ι->ProgInstPK{ return Pks().Instance; }
		α ConnectionPK()Ι->ConnectionPK{ return Pks().Connection; }
		α UserPK()Ι->Jde::UserPK override{ return Session() ? Session()->UserPK : Jde::UserPK{}; }//the adopted session is the only identity this socket has - see SessionUserPK.
	private:
		α OnRead( Proto::FromClient::Transmission&& transmission )ι->void override;
		α OnClose()ι->void override;//OnDisconnect is not overridden: the base now routes it here (#6), which is all this override did.
		α GetJwt( Jde::RequestId requestId )ι->TAwait<jobject>::Task;
		α Login( string&& jwt, RequestId requestId )ι->TAwait<sp<Web::Server::SessionInfo>>::Task;
		α ProcessTransmission( Proto::FromClient::Transmission&& transmission, optional<Jde::UserPK> userPK, optional<RequestId> clientRequestId, uint8 depth )ι->void;
		α SendQueryClient( QL::TableQL&& query, Jde::UserPK executer, RequestId requestId )ι->void override;
		α SharedFromThis()ι->sp<ServerSocketSession>{ return std::static_pointer_cast<ServerSocketSession>(shared_from_this()); }
		α TestAdmin( str resource, str criteria, Jde::UserPK userPK, SRCE )ι->up<AnyVoidAwait> override;
		α WriteException( runtime_error&& e, RequestId requestId, SRCE )ι->void override;
		α WriteException(std::string&&, Jde::RequestId, SL sl )ι->void override;
		α WriteSubscriptionAck( flat_set<QL::SubscriptionId>&& subscriptionIds, RequestId requestId )ι->void override;
		α WriteSubscription( const jvalue& j, RequestId requestId )ι->void override;
		α WriteSubscription( App::ProgramPK appPK, App::ProgInstPK instancePK, const Logging::Entry& e, const QL::Subscription& sub )ι->void override;
		α WriteComplete( RequestId requestId )ι->void override;

		α AddSession( Proto::FromClient::AddSession addSession, RequestId clientRequestId, SL sl )ι->AddSessionAwait::Task;
		α AddInstance( Proto::FromClient::Instance instance, RequestId requestId )ι->TAwait<sp<Web::Server::SessionInfo>>::Task;
		α LocalQL()Ι->sp<QL::IQL> override;
		α Execute( string&& bytes, optional<Jde::UserPK> userPK, RequestId clientRequestId, uint8 depth )ι->void;
		α ForwardExecution( Proto::FromClient::ForwardExecution&& clientMsg, bool anonymous, RequestId clientRequestId, SRCE )ι->ForwardExecutionAwait::Task;
		α GraphQL( string&& query, jobject variables, bool returnRaw, RequestId requestId, optional<Jde::UserPK> executer )ι->QL::QLAwait<jvalue>::Task;
		α SaveLogEntry( Log::Proto::LogEntryClient logEntry, RequestId requestId )ι->void;
		α SendAck( uint32 id )ι->void override;
		α SessionInfo( SessionPK sessionId, RequestId requestId )ι->void;
		α SetSessionId( SessionPK sessionId, RequestId requestId )->Web::Server::Sessions::UpsertAwait::Task;
		α Adopt( SessionPK sessionId, SRCE )ι->Web::Server::Sessions::UpsertAwait;//binds this socket to an existing web session (kInstance, kSessionId).
		α WriteFailedAdoption( runtime_error&& e, RequestId requestId )ι->void;//then closes the socket after too many failed adoptions - a 32-bit id is otherwise cheap to brute-force.

		Proto::FromClient::Instance _instance;
		App::ConnectionPks _pks;
		mutable mutex _registrationMutex;//guards _instance and _pks together: AddInstance publishes both on this session's strand while _sessions visitors read them from other threads.
		SessionPK _instanceSessionId{};//the web session kInstance adopted - removed in OnClose.
		uint8 _failedAdoptions{};
	};
}
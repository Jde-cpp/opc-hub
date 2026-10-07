#pragma once
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/process/process.h>
#include <jde/web/server/usings.h>
#include <jde/web/server/Sessions.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/ql/usings.h>
#include <jde/ql/QLAwait.h>
#include "QueryClientAwait.h"

namespace Jde::DB{ struct AppSchema; }
namespace Jde::Proto{ class Query; }
namespace Jde::QL{ struct Subscription; }

namespace Jde::Web::Server{
	struct IRestStream; struct ISocketStream; template<class TStream> struct SocketStream;
	struct ΓWS IWebsocketSession : std::enable_shared_from_this<IWebsocketSession>{
		IWebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι;
		α Run()ι->void;
		α Id()Ι->SocketId{ return _id; }
		//The one counter for every request this server sends *this* session, whatever kind - QueryClient below and the
		//AppServer's forwarded executions both draw from it, so the id in a reply can only belong to one of them (finding
		//#12 of appserver-review3).  Per-session, so an id is only unique together with the connection it was sent to.
		α NextRequestId()ι->RequestId{ return ++_requestId; }
		α QueryClient( QL::TableQL query, Jde::UserPK executer, SRCE )ι->QueryClientAwait{ return QueryClientAwait{move(query), executer, shared_from_this(), sl}; }
		α QueryClient( QL::TableQL&& query, Jde::UserPK executer, QueryClientAwait::Handle h, SRCE )ι->void;
		α AddSubscription( string&& query, jobject variables, RequestId requestId, Jde::UserPK executer, SRCE )ε->flat_set<QL::SubscriptionId>;
		α LogWrite( string&& what, RequestId requestId, ELogLevel level=ELogLevel::Trace, SRCE )ι->void;
		α RemoveSubscription( vector<QL::SubscriptionId>&& ids, RequestId requestId, SRCE )ι->void;
		β WriteSubscription( const jvalue& j, RequestId requestId )ι->void=0;
		β WriteSubscription( uint32 appPK, uint32 appInstancePK, const Logging::Entry& e, const QL::Subscription& sub )ι->void=0;
		β WriteSubscriptionAck( flat_set<QL::SubscriptionId>&& subscriptionIds, RequestId requestId )ι->void=0;
		β WriteComplete( RequestId requestId )ι->void=0;
		β WriteException( runtime_error&& e, RequestId requestId, SRCE )ι->void=0;
		β WriteException( string&& e, RequestId requestId, SL sl )ι->void=0;
		β UserPK()Ι->Jde::UserPK=0;
		α IsOpen()ι->bool{ return StreamPtr()!=nullptr; }//OnClose nulls _stream, so this is the one place that knows the socket behind a registration is gone.
		α SessionId()ι{ return _sessionInfo ? _sessionInfo->SessionId : SessionPK{}; }//public: Sessions::Remove has to find the sockets bound to a revoked id (#5).
		β Close()ι->void;
	protected:
		α StreamPtr()ι->sp<ISocketStream>{ lg _{ _streamMutex }; return _stream; }//_stream is written by OnClose on the strand & read from other threads (Write/Close) - always copy through here outside the strand.
		tcp::endpoint _userEndpoint;
		β OnClose()ι->void;
		β OnRead( const char* p, uint size )ι->void=0;
		β SendAck( uint32 id )ι->void=0;
		β Query( Proto::Query&& query, RequestId requestId, function<string(string&&, RequestId)>&& toProtoString )ι->QL::QLAwait<jvalue>::Task;

		α LogRead( string&& what, RequestId requestId, ELogLevel level=ELogLevel::Trace, ELogTags tags=ELogTags::SocketServerRead, SRCE )ι->void;
		α LogWriteException( const runtime_error& e, RequestId requestId, ELogLevel level=ELogLevel::Debug, SRCE )ι->void;
		α LogWriteException( str e, RequestId requestId, ELogLevel level=ELogLevel::Debug, SRCE )ι->void;
		α QueryClientResults( string&& queryResult, RequestId requestId )ι->void;
		α ResumeQueryException( RequestId requestId, Exception&& e )ι->bool;//fails a pending QueryClient; false if requestId isn't one of ours, so the caller can try another router (e.g. a forwarded execution).
		α Schemas()Ι->const vector<sp<DB::AppSchema>>&{ return LocalQL()->Schemas(); }
		α Session()Ι->const sp<SessionInfo>&{ return _sessionInfo; }
		α SetSessionId( SessionPK sessionId )ι->void;
		α SetSessionInfo( sp<SessionInfo> sessionInfo )ι->void{ _sessionInfo = move(sessionInfo); }
		α Write( string&& m )ι->void;

	private:
		α AddTimeout( RequestId requestId, QueryClientAwait::Handle h, Duration timeout, SRCE )ι->TimerAwait::Task;
		α TakePending( RequestId requestId, bool erase=false )ι->optional<QueryClientAwait::Handle>;
		//#6: every read error other than websocket::error::closed comes here (Streams.cpp DoRead) - beast's idle timeout,
		//connection_reset, message_too_big.  OnClose is the safe default: a `{}` base silently leaked the session, its
		//SocketServerListener<->session sp cycle and its fd on any peer that vanished without a close frame.  An override must add
		//to this, not replace it - call OnClose() (or the base) on every path.
		β OnDisconnect( CodeException&& )ι->void{ OnClose(); }
		β OnAccept( beast::error_code ec )ι->void;

		α OnRun()ι->void;
		α DoRead()ι->void;
		β SendQueryClient( QL::TableQL&& query, Jde::UserPK executer, RequestId requestId )ε->void=0;//the wire write - QueryClient above registers the request first.
		β LocalQL()Ι->sp<QL::IQL> = 0;

		const SocketId _id{}; // index starts at 0 for each app start.
		mutex _streamMutex;//guards _stream only - everything else session-related runs on the stream's strand.
		sp<ISocketStream> _stream;
		TRequestType _initialRequest;
		sp<QL::IListener> _listener;
		flat_map<RequestId, std::pair<QueryClientAwait::Handle, sp<DurationTimer>>> _pendingQueries; mutex _pendingQueriesMutex;
		atomic<RequestId> _requestId;
		sp<SessionInfo> _sessionInfo;
		template<class> friend struct SocketStream;
	};

	template<class TFromServer, class TFromClient>
	struct TWebsocketSession /*abstract*/ : IWebsocketSession{
		TWebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType request, tcp::endpoint userEndpoint, uint32 connectionIndex )ι :
			IWebsocketSession{ move(stream), move(buffer), move(request), move(userEndpoint), connectionIndex }{}

		α OnRead( const char* p, uint size )ι->void;
		β OnRead( TFromClient&& transmission )ι->void = 0;
		α Write( TFromServer&& message )ι->void;
	};

#define $ template<class TFromServer, class TFromClient> auto TWebsocketSession<TFromServer,TFromClient>
	$::OnRead( const char* p, uint size )ι->void{
		try{
			auto t = Protobuf::Deserialize<TFromClient>( (const google::protobuf::uint8*)p, size );
			OnRead( move(t) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), RequestId{0} );
		}
	}

	$::Write( TFromServer&& message )ι->void{
		IWebsocketSession::Write( message.SerializeAsString() );
	}
}
#undef $
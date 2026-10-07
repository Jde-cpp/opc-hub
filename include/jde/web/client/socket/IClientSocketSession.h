#pragma once
#include <boost/unordered/concurrent_flat_map.hpp>
#include "ClientSocketStream.h"
#include "ClientSocketAwait.h"
#include "jde/fwk/process/process.h"
#include <jde/fwk/co/Await.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/io/protobuf.h>

namespace Jde::Web::Client{
	struct IClientSocketSession; template<class TWs> struct ClientSocketStream;
	struct CreateClientSocketSessionAwait final : VoidAwait{
		using base = VoidAwait;
		CreateClientSocketSessionAwait( sp<IClientSocketSession> session, string host, PortType port, string target="/", SRCE )ι;
		α Suspend()ι->void override;
	private:
		sp<IClientSocketSession> _session; string _host; PortType _port; string _target;
	};

	struct CloseClientSocketSessionAwait final : VoidAwait{
		using base = VoidAwait;
		CloseClientSocketSessionAwait( sp<IClientSocketSession> session, bool terminate, SRCE )ι:base{sl}, _session{session}, _terminate{terminate}{};
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
	private:
		sp<IClientSocketSession> _session;
		bool _terminate;
	};

	//TODO check what should be protected
	struct IClientSocketSession : IShutdown, std::enable_shared_from_this<IClientSocketSession>{
		IClientSocketSession( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι;// Resolver and socket require an io_context
		virtual ~IClientSocketSession()=default;
		α Shutdown( bool terminate, SL sl )ι->void override;
		α AddTask( RequestId requestId, PendingTask&& task )ι->void;
		//started per request alongside AddTask: if nothing has answered by the deadline the caller would otherwise wait forever.
		α AddTimeout( RequestId requestId, SRCE )ι->TimerAwait::Task;
		α PopTask( RequestId requestId )ι->PendingTask;//empty if requestId is not pending:  answered, failed, or never a request.

		α Run( string host, PortType port, string target, CreateClientSocketSessionAwait::Handle h )ι->void;// Start the asynchronous operation
		//target: the websocket handshake's request path - "/" for every server but a host that routes protocols by path (OpcHub: "/opc" for the gateway's).
		α RunSession( string host, PortType port, string target="/" )ι{ return CreateClientSocketSessionAwait{shared_from_this(), host, port, move(target)}; }
		β Query( string&& query, jobject variables, bool returnRaw, SRCE )ι->ClientSocketAwait<jvalue> = 0;
		β Subscribe( string&& query, jobject variables, sp<QL::IListener> listener, SRCE )ε->ClientSocketAwait<jarray> = 0;
		β Unsubscribe( vector<QL::SubscriptionId>&& ids, SRCE )ι->void=0;
		α Write( string&& m )ι->void;
		α NextRequestId()ι->uint32;
		α SessionId()ι->SessionPK{ return _sessionInfo ? _sessionInfo->session_id() : SessionPK{}; }
		α SetInfo( Web::FromServer::SessionInfo&& info )ι->void{ _sessionInfo = move(info); }
		α UserPK()Ι->UserPK{ return  { _sessionInfo ? _sessionInfo->user_pk() : 0}; }
		[[nodiscard]] α Close( bool terminate, SL sl )ι{ return CloseClientSocketSessionAwait(shared_from_this(), terminate, sl); }
		α Host()Ι->str{ return _host; }
		α Id()ι->uint32{ return _id; }
	protected:
		//Fails every pending task - the socket is gone, so none can be answered.  An override adds its own bookkeeping around this.
		β CloseTasks( beast::error_code ec )ι->void;
		//C6: a request that can never be answered drops the session, and OnClose fails every pending task through CloseTasks.  The
		//app client reconnects on close, so this is recoverable rather than fatal.
		α CloseOnError( string reason, SRCE )ι->void;
		α HasTask( RequestId requestId )Ι->bool{ return _tasks.contains( requestId ); }
		//Cancel the deadline AddTimeout armed for this request, if it is still pending.  A DurationTimer that is never
		//cancelled is *live asio work*: `io_context::run` cannot return while one is queued, so an answered request used to
		//hold the executor - and with it every shutdown - for the balance of its full requestTimeout.
		α CancelTimeout( RequestId requestId )ι->void;
		α CancelTimeouts()ι->void;//every one of them - the socket is gone, so nothing they guard can still be answered.
		β OnClose( beast::error_code ec )ι->void;
		//mirrors the server's StreamPtr (IWebsocketSession.h): _stream is written by OnClose on the strand and read from other
		//threads - Write from any caller, Close from a shutdown thread - so always take a copy through here, never touch the
		//member directly, and treat null as "already closed".
		α StreamPtr()Ι->sp<IClientSocketStream>{ lg _{ _streamMutex }; return _stream; }
		α IsSsl()Ι->bool{ auto stream = StreamPtr(); return stream && stream->IsSsl(); }
		α SetId( uint32 id )ι{ _id=id; }
		α LogRead( string&& what, SRCE )ι->void{ LOGSL( ELogLevel::Trace, sl, ELogTags::SocketClientRead, "{}", move(what) ); }//text formatted by the caller, as the server's IWebsocketSession::LogRead takes it.
	private:
		α OnResolve( beast::error_code ec, tcp::resolver::results_type results )ι->void;
		α OnConnect( beast::error_code ec, tcp::resolver::results_type::endpoint_type ep )ι->void;
		α OnSslHandshake(beast::error_code ec )ι->void;
		α OnHandshake( beast::error_code ec )ι->void;
		α OnRead( beast::error_code ec, uint bytes_transferred )ι->void;
		β OnReadData( std::span<uint8_t> transmission )ι->void=0;

		tcp::resolver _resolver;
		mutable std::mutex _streamMutex;
		sp<IClientSocketStream> _stream;
		string _host;
		string _target{ "/" };
		sp<net::io_context> _ioContext;
		optional<Web::FromServer::SessionInfo> _sessionInfo;
		CreateClientSocketSessionAwait::Handle _connectHandle;
		CloseClientSocketSessionAwait::Handle _closeHandle;
		boost::concurrent_flat_map<RequestId,PendingTask> _tasks;
		//The deadline per in-flight request, so answering one can cancel it.  The server side has always kept its timers this
		//way (IWebsocketSession::_pendingQueries, finding S3); the client kept only the handle and paid the full timeout.
		boost::concurrent_flat_map<RequestId,sp<DurationTimer>> _timeouts;
		atomic<uint32> _id;//_serverSocketIndex

		template<class> friend struct ClientSocketStream; friend struct CloseClientSocketSessionAwait;
	};

	template<class TFromClientMsgs, class TFromServerMsgs>
	struct TClientSocketSession : IClientSocketSession{
		using base=IClientSocketSession;
		TClientSocketSession( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι:IClientSocketSession{ ioc, ctx }{}
		α Write( TFromClientMsgs&& m )ι->void;
	protected:
		α OnReadData( std::span<uint8_t> transmission )ι->void override;
		β OnRead( TFromServerMsgs&& m )ι->void=0;
	};

	#define $ template<class TFromClientMsgs, class TFromServerMsgs> α TClientSocketSession<TFromClientMsgs,TFromServerMsgs>
	$::Write( TFromClientMsgs&& m )ι->void{ base::Write( Protobuf::ToString(m) ); }
	$::OnReadData( std::span<uint8_t> transmission )ι->void{
		try{
			auto proto = Protobuf::Deserialize<TFromServerMsgs>( transmission.data(), transmission.size() );
			OnRead( move(proto) );
		}
		catch( Exception& e ){
			//The requestId lives inside the payload that just failed to parse, so there is no way to tell which caller was waiting
			//on it.  Swallowing it left every pending request hanging until the socket happened to close; an undecodable
			//transmission means the stream is no longer trustworthy, so drop it and let CloseTasks fail them all.
			e.SetTags( ELogTags::SocketClientRead );
			base::CloseOnError( Ƒ("undecodable transmission ({} bytes): {}", transmission.size(), e.what()) );
		}
	}
}
#undef $
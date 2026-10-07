#include <jde/web/client/socket/IClientSocketSession.h>
#include <boost/asio/error.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/websocket/error.hpp>
#include <jde/fwk.h>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/log/logTags.h>
#include <jde/fwk/usings.h>
#include <jde/app/client/clientSubscriptions.h>
#include <jde/fwk/process/execution.h>

namespace Jde{
	constexpr ELogTags _connectTag{ ELogTags::Socket | ELogTags::Client };
	constexpr ELogTags _connectPedanticTag{ ELogTags::Socket | ELogTags::Client | ELogTags::Pedantic };
	constexpr ELogTags _writeTag{ ELogTags::SocketClientWrite };
	constexpr ELogTags _readTag{ ELogTags::SocketClientRead };

	static optional<uint16> _maxLogLength;
	α Web::MaxLogLength()ι->uint16{
		if( !_maxLogLength )
			_maxLogLength = Settings::FindNumber<uint16>( "/http/maxLogLength" ).value_or( 255 );//L5: leading slash - without it the path never matched and the default always won, so the setting was inert on the client side.
		return *_maxLogLength;
	}

	//compare codes, not ec.value(): values are only unique within a category, and beast's end_of_stream, asio's stream_truncated
	//and errno's EPERM are all 1.  switching on the value quieted normal keep-alive closes only by that collision, and equally
	//dropped a genuine category-1 error to Trace.
	α Web::ErrorSeverity( beast::error_code ec, EErrorRole role )ι->ELogLevel{
		using enum ELogLevel;
		switch( role ){
		case EErrorRole::HttpServerRead:
			if( ec==net::error::operation_aborted )
				return Debug;
			if( ec==http::error::end_of_stream )//peer closed a keep-alive connection - how a session normally ends.
				return Trace;
			if( ec==ssl::error::stream_truncated )//an SSL "short read": peer closed without performing the required closing handshake.
				return Trace;
			//ERR_SSL_SSLV3_ALERT_CERTIFICATE_UNKNOWN: the client doesn't trust the certificate.  openssl packs its own codes and asio
			//has no enumerator to name them, so this one is matched on category+value.
			if( ec.category()==net::error::get_ssl_category() && ec.value()==0xA000416 )
				return Trace;
			return Error;
		case EErrorRole::HttpServerWrite:
			return ec==beast::error::timeout ? Debug : Error;
		case EErrorRole::SocketServerRead:
			return Debug;//every ending of a socket read - closed, timeout, reset, aborted - is routine here; the session's OnDisconnect decides what it means.
		case EErrorRole::SocketClient:
			if( ec==net::error::operation_aborted || ec==websocket::error::closed )
				return Debug;
			if( ec==net::error::eof || ec==net::error::connection_reset )//server down.
				return Information;
			return Error;
		case EErrorRole::HttpClientShutdown:
			return ec==net::error::eof || ec==beast::errc::not_connected ? NoLog : Trace;//the peer closed first, or the connection never came up.
		}
		return Error;
	}
}
#define CHECK_EC( tag ) if( ec ){ \
	CodeException e{ static_cast<std::error_code>(ec), tag, ErrorSeverity(ec, EErrorRole::SocketClient) }; \
	if( auto h = _connectHandle; h ){ \
		_connectHandle = nullptr; \
		h.promise().SetExp( move(e) ); \
		h.resume(); \
	}\
	return; \
}
namespace Jde::Web::Client{
	α IClientSocketSession::Shutdown( bool terminate, SL sl )ι->void{
		if( _ioContext ){
			TRACET( _connectTag, "[{}]Client::Shutdown: {}", hex(Id()), Host() );
			BlockVoidAwait( Close(terminate, sl) );
		}
	}

	α IClientSocketSession::AddTask( RequestId requestId, PendingTask&& task )ι->void{
		_tasks.emplace( requestId, move(task) );
	}

	α IClientSocketSession::PopTask( RequestId requestId )ι->PendingTask{
		PendingTask y;
		_tasks.erase_if( requestId, [&y](auto&& kv){ y = move(kv.second); return true; } );//Subscriptions aren't in tasks.
		CancelTimeout( requestId );//answered - stop its deadline from holding the io_context for the rest of the timeout.
		return y;
	}
	α IClientSocketSession::CancelTimeout( RequestId requestId )ι->void{
		_timeouts.visit( requestId, [](auto&& kv){ kv.second->Cancel(); } );//AddTimeout's own resumption erases the entry.
	}
	α IClientSocketSession::CancelTimeouts()ι->void{
		_timeouts.visit_all( [](auto&& kv){ kv.second->Cancel(); } );
	}
	//60s: long enough that a slow query is not mistaken for a dead peer, short enough that a stranded caller does not wait out the
	//process.  Whether it is right depends on the workload, hence the setting - a legitimate query that outlives it takes the
	//session down and reconnects, which is worse than waiting.
	Duration _socketRequestTimeout{};
	Ω requestTimeout()ι->Duration{
		auto value = _socketRequestTimeout;
		if( value==Duration::zero() )
			_socketRequestTimeout = value = Settings::FindDuration( "/web/client/socketRequestTimeout" ).value_or( std::chrono::seconds(60) );
		return value;
	}

	α IClientSocketSession::CloseOnError( string reason, SL sl )ι->void{
		Exception{ sl, ELogLevel::Error, "[{}]Closing socket: {}", Ƒ("{:x}", Id()), reason };
		if( auto stream = StreamPtr(); stream )
			stream->Close( shared_from_this(), false, sl );//OnClose drains _tasks with the close reason.
		else
			//#15: no stream, so nothing was going to drain _tasks.  This is the *recovery* path - AddTimeout calls it precisely
			//because a request went unanswered
			CloseTasks( net::error::not_connected );
	}

	α IClientSocketSession::AddTimeout( RequestId requestId, SL sl )ι->TimerAwait::Task{
		const auto _ = shared_from_this();//the timer outlives the request; keep us alive so the check below is not on a freed session.
		const auto timeout = requestTimeout();
		auto timer = ms<DurationTimer>( timeout, sl );
		//Registered before the first suspend, which is before Suspend() writes the request (ClientSocketAwait), so no reply can
		//arrive ahead of the entry PopTask cancels through.  Waiting the timer out instead is not free: a pending asio timer is
		//work, and io_context::run - hence Process::Shutdown - blocks on it.  A process that exited within the timeout of its
		//last request therefore sat out the remainder and was killed by the shutdown watchdog (emulator-review #11).
		_timeouts.emplace( requestId, timer );
		auto _ = co_await *timer;
		_timeouts.erase( requestId );
		if( !HasTask(requestId) )
			co_return;//answered, or already failed with the session.
		CloseOnError( Ƒ("request {} unanswered after {}", hex(requestId), Chrono::ToString(timeout)), sl );
	}

	α IClientSocketSession::CloseTasks( beast::error_code ec )ι->void{
		CancelTimeouts();//the socket is gone; nothing these guard can still be answered, and each one is live io_context work.
		vector<PendingTask> tasks;
		_tasks.erase_if( [&tasks]( auto&& kv ){ tasks.push_back( move(kv.second) ); return true; } );
		for( auto& task : tasks ){//failed outside the map:  a resumed caller may issue its next request, and the map cannot be entered from its own visitor.
			if( ec )
				task.Fail( CodeException{static_cast<std::error_code>(ec), ELogTags::SocketClientWrite, ELogLevel::NoLog} );
			else
				task.Fail( Exception{SRCE_CUR, ELogLevel::NoLog, "Session closed."} );
		}
	}

	CreateClientSocketSessionAwait::CreateClientSocketSessionAwait( sp<IClientSocketSession> session, string host, PortType port, string target, SL sl )ι:
		base{ sl },
		_session{ session },
		_host{ host },
		_port{ port },
		_target{ move(target) }
	{}

	α CreateClientSocketSessionAwait::Suspend()ι->void{
		_session->Run( _host, _port, move(_target), _h );
		_session = nullptr;
	}

	atomic<RequestId> _requestId{ 1 };
	α IClientSocketSession::NextRequestId()ι->RequestId{ return _requestId++; }

	IClientSocketSession::IClientSocketSession( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι:
		_resolver{ *ioc },
		_stream{ IClientSocketStream::Create(*ioc, ctx) },
		_ioContext{ ioc }
	{}

	α IClientSocketSession::Run( string host, PortType port, string target, CreateClientSocketSessionAwait::Handle h )ι->void{ // Start the asynchronous operation
		_connectHandle = h;
		_host = host;
		_target = target.empty() ? "/" : move(target);
		TRACET( _connectPedanticTag, "[{}:{}]resolve socket.", _host, port );
		_resolver.async_resolve( _host, std::to_string(port), beast::bind_front_handler(&IClientSocketSession::OnResolve, shared_from_this()) );
	}

	α IClientSocketSession::OnResolve( beast::error_code ec, tcp::resolver::results_type results )ι->void{
		CHECK_EC( _writeTag )
		TRACET( _connectPedanticTag, "[{}]resolve succeeded.", _host );
		if( auto stream = StreamPtr(); stream )
			stream->OnResolve( results, shared_from_this() );
	}

	α IClientSocketSession::OnConnect( beast::error_code ec, tcp::resolver::results_type::endpoint_type ep )ι->void{
		CHECK_EC( _readTag )
		TRACET( _connectPedanticTag, "[{}]connect succeeded.", _host );
		if( auto stream = StreamPtr(); stream )
			stream->OnConnect( ep, _host, shared_from_this() );
	}

	α IClientSocketSession::OnSslHandshake( beast::error_code ec )ι->void{
		CHECK_EC( _readTag )
		TRACET( _connectPedanticTag, "[{}]SslHandshake succeeded.", _host );
		if( auto stream = StreamPtr(); stream )
			stream->AfterHandshake( _host, shared_from_this() );
	}

	α IClientSocketSession::OnHandshake( beast::error_code ec )ι->void{
		CHECK_EC( _readTag )
		DBGT( _connectTag, "[{}]OnHandshake succeeded. Calling read.", _host );
		if( auto h = _connectHandle; h ){
			_connectHandle = nullptr;
			h.resume();
		}
		if( auto stream = StreamPtr(); stream )
			stream->AsyncRead( shared_from_this() );
	}
	α IClientSocketSession::Write( string&& m )ι->void{
		//the hot cross-thread case: a caller writing while a close is running on the strand used to dereference a nulled _stream.
		if( auto stream = StreamPtr(); stream ){
			stream->AsyncWrite( move(m), shared_from_this() );
			return;
		}
		//#15: the frame used to be dropped here in silence, while Suspend had already registered the request in _tasks - so
		//nothing could ever answer it.  Everything pending is equally undeliverable once the stream is gone; fail it now rather
		//than leave the caller to the request timeout (and, before the fix above, to nothing at all).
		//Posted, not inline: Write is reached from ClientSocketAwait::Suspend, i.e. from inside await_suspend, and resuming the
		//caller there re-enters a coroutine that has not finished suspending - the trap CloseClientSocketSessionAwait::await_ready
		//documents.  The post lets await_suspend return first.
		DBGT( _writeTag, "[{}]Write on a closed session - failing the pending request(s).", hex(Id()) );
		Post( [self=shared_from_this()]{ self->CloseTasks( net::error::not_connected ); } );
	}

	α IClientSocketSession::OnRead( beast::error_code ec, uint bytes_transferred )ι->void{
		boost::ignore_unused( bytes_transferred );
		if( ec ){
			CodeException{ static_cast<std::error_code>(ec), _readTag, Ƒ("[{:x}]ClientSocket::DoRead", Id()), ErrorSeverity(ec, EErrorRole::SocketClient) };
			if( ec==net::error::operation_aborted )// our own in-flight Close() cancelled this read; its OnClose completion will drain _tasks with the real close reason, so don't preempt it with a misleading "operation_aborted" one here.
				return;
			// websocket::error::closed means the close handshake already completed (Beast auto-replies to a received close frame);
			// calling Close() again would initiate a second async_close that collides with the in-flight one on Beast's write
			// soft_mutex.  Braced, not a bare `else if` - an `else` after an `if` that guards on StreamPtr binds to the inner one.
			if( ec==boost::beast::websocket::error::closed ){
				//Since no async_close of ours runs, nothing would otherwise call OnClose - and the teardown it owns is not
				//optional.  Draining _tasks alone left _stream and _ioContext live, IAppClient::Connected() still true and no
				//reconnect scheduled, so a routine server restart wedged the client until the process died.  Do the whole
				//teardown here - unless our own Close() is already in flight, in which case its async_close completion is the
				//call that fires OnClose and repeating it here would run the derived reconnect twice.
				if( auto stream = StreamPtr(); stream && stream->IsClosing() )
					CloseTasks( ec );
				else
					OnClose( ec );
			}
			else if( auto stream = StreamPtr(); stream )
				stream->Close( shared_from_this(), false, SRCE_CUR );
			return;
		}
		auto stream = StreamPtr();
		if( !stream )
			return;
		OnReadData( stream->ReadBuffer() );
		stream->AsyncRead( shared_from_this() );
	}
	//Nothing to wait on: no stream, or a close already running.  OnClose resumes the *first* waiter and only then nulls _stream, so
	//a caller that closes again on waking finds a live-but-closing stream - and ClientSocketStream::Close early-returns on _closing
	//without completing anyone's await, so that second caller would wait out the process.
	//Answered here rather than resuming from Suspend: calling Resume() inside await_suspend re-enters a coroutine that has not
	//finished suspending.
	α CloseClientSocketSessionAwait::await_ready()ι->bool{
		auto stream = _session->StreamPtr();
		return !stream || stream->IsClosing();
	}
	α CloseClientSocketSessionAwait::Suspend()ι->void{
		_session->_closeHandle = _h;
		if( auto stream = _session->StreamPtr(); stream )
			stream->Close( _session, _terminate );
	}
	α IClientSocketSession::OnClose( beast::error_code ec )ι->void{
		if( ec )
			CodeException{ static_cast<std::error_code>(ec), _readTag, Ƒ("[{}]Client::OnClose: {}", hex(Id()), _host), ErrorSeverity(ec, EErrorRole::SocketClient) };
		else
			DBGT( _connectTag, "[{}]Client::OnClose: {}", hex(Id()), _host );
		CloseTasks( ec );
		if( _closeHandle )
			_closeHandle.resume();
		_closeHandle = nullptr;
		{
			lg _{ _streamMutex };
			_stream = nullptr;
		}
		_ioContext = nullptr;
	}
}
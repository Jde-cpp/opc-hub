#pragma once
#include <deque>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include "usings.h"

namespace Jde::Web{
	constexpr auto CloseTimeout{ 5s };//how long Close waits for a pending write & the close handshake before tearing down the transport.

	//The half of a websocket stream that is the same on both sides: the strand-confined write queue and the close state machine.
	//The server's SocketStream<> and the client's ClientSocketStream<> are accept vs. connect/handshake adapters over it, and own their reads.
	//TWs: websocket::stream<>.  TSession: what a queued frame and a close keep alive; handed back through the two hooks.
	template<class TWs, class TSession>
	struct SocketCore: std::enable_shared_from_this<SocketCore<TWs,TSession>>{
		using StrandType = net::strand<net::io_context::executor_type>;
		virtual ~SocketCore()=default;
		//Close early-returns on an already-closing stream without calling anyone's OnClosed, so callers that wait on it have to ask first.
		α IsClosing()Ι->bool{ return _closing.test(); }
	protected:
		//Every completion below is bound to strand, so wsArgs may put the stream on it (client) or on some other executor (server: moved from an http stream built on the accept executor).
		template<class... TArgs> SocketCore( StrandType strand, TArgs&&... wsArgs )ι:_strand{ move(strand) }, _ws{ FWD(wsArgs)... }{}
		α Write( string&& buffer, sp<TSession> session )ι->void;//any thread.
		α Close( sp<TSession> session, websocket::close_code code=websocket::close_code::normal )ι->void;//any thread; idempotent.
		//strand.  The end of every close, exactly once.  ec: async_close's result; beast::error::timeout when the deadline closed
		//the transport before a close handshake could start; empty when the stream never opened.
		β OnClosed( sp<TSession> session, beast::error_code ec )ι->void=0;
		//strand.  The queue carries on (or finishes a pending close) afterwards; an implementation may Close.
		β OnWriteError( sp<TSession> session, beast::error_code ec )ι->void=0;

		StrandType _strand;//serializes every op on _ws: the ioc is multithreaded and beast streams aren't thread-safe, so Close (shutdown thread) would otherwise race read/write handlers.
		TWs _ws;
		bool _open{};//strand-confined. Handshake completed - async_close is only valid on an open stream.
	private:
		α DoWrite()ι->void;//strand. Starts the queue head; its completion starts the next.
		α DoClose()ι->void;//strand. The single OnClosed path - runs once, and only with no write outstanding.

		//#9: a strand-confined write queue, where a CoLock used to be.  The lock was held from before async_write until its
		//completion handler, and a *contended* Lock() resumes its waiter through CoLock::Clear's Post() - the raw io_context -
		//so the next beast op was initiated off-strand, possibly while the strand ran a read's completion.  beast streams are not
		//thread-safe.  The queue expresses the same rule (beast allows one outstanding write-type op) without ever leaving the
		//strand: everything below is touched only from a handler on _strand, so there is no lock to contend and no hop back.
		struct WriteItem{
			WriteItem( string&& buffer, sp<TSession>&& session )ι:Buffer{ move(buffer) }, Session{ move(session) }{}
			string Buffer;
			sp<TSession> Session;//keeps a queued frame's session alive, as the old completion-handler capture did.
		};
		std::deque<WriteItem> _writeQueue;//strand-confined.  deque, not vector: async_write holds a reference into front() while later frames are appended behind it.
		bool _writing{};//strand-confined. An async_write is outstanding - beast forbids a second write-type op (async_close included) until it completes.  This is what the CoLock expressed.
		sp<TSession> _closeSession;//strand-confined. Held from Close until OnClosed - a close that has to wait out a write still needs the session afterwards.  Also the once-only token for DoClose.
		websocket::close_code _closeCode{ websocket::close_code::normal };//strand-confined. Parked until the close actually starts.
		sp<net::steady_timer> _closeDeadline;//strand-confined.
		bool _transportClosed{};//strand-confined. Close deadline fired - lowest layer closed, no close handshake possible.
		std::atomic_flag _closing;//Close initiated - makes Close idempotent & drops later reads/writes.  Atomic, not strand-confined: set by Close on the caller's thread and read off-strand through IsClosing().
	};

#define $ template<class TWs, class TSession> auto SocketCore<TWs,TSession>
	$::Write( string&& buffer, sp<TSession> session )ι->void{
		net::dispatch( _strand, [this, self=this->shared_from_this(), buffer=move(buffer), session=move(session)]()mutable{//self keeps the stream alive across the hop - OnClose can drop the session's ref while we're queued.
			if( _closing.test() )
				return;//a close frame is itself a write-type op, so nothing may be initiated once Close has started.
			_writeQueue.emplace_back( move(buffer), move(session) );
			if( !_writing )
				DoWrite();//else the in-flight write's completion picks this up.
		});
	}

	//strand.  One outstanding async_write at a time - the queue is what serializes them, and its completion drives the next.
	$::DoWrite()ι->void{
		_writing = true;
		_ws.async_write( net::buffer(_writeQueue.front().Buffer), net::bind_executor(_strand, [this, self=this->shared_from_this()]( beast::error_code ec, uint bytes )mutable{
			const auto expected = _writeQueue.front().Buffer.size();
			auto session = move( _writeQueue.front().Session );
			_writeQueue.pop_front();
			_writing = false;
			if( ec || expected!=bytes )
				OnWriteError( move(session), ec );
			if( _closeSession )//Close arrived while this write was outstanding and left the finish to us.
				DoClose();
			else if( !_writeQueue.empty() && !_closing.test() )//closing without a parked session: Close's hop is still queued behind us and finishes itself.
				DoWrite();
		}) );
	}

	$::Close( sp<TSession> session, websocket::close_code code )ι->void{
		if( _closing.test_and_set() )
			return;//already closing - a second async_close would overlap the first (write-type op).
		net::dispatch( _strand, [this, self=this->shared_from_this(), session=move(session), code]()mutable{//self keeps the stream alive across the hop, and until the deadline resolves either way.
			_closeSession = move( session );
			_closeCode = code;
			//A close frame is a write: a peer that stopped reading (TCP backpressure) would stall it - or the write it is queued
			//behind - indefinitely, so OnClosed never fires: the server wedges shutdown at the executor drain and the client
			//(#13) hangs IClientSocketSession::Shutdown's BlockVoidAwait.  On deadline, close the transport; the aborted op
			//completes on the strand and DoClose finishes without a handshake.
			if( _open ){
				_closeDeadline = ms<net::steady_timer>( _strand, CloseTimeout );
				_closeDeadline->async_wait( net::bind_executor(_strand, [this, self]( beast::error_code ec ){
					if( ec )
						return;//cancelled - close completed in time.
					_transportClosed = true;
					beast::get_lowest_layer( _ws ).close();
				}) );
			}
			if( !_writing )
				DoClose();//else the write's completion runs it - async_close must not overlap a pending write.
		});
	}

	//strand.  Every ending funnels here - Close(), and a failed write.  _closeSession is the once-only token: OnClosed runs exactly once.
	$::DoClose()ι->void{
		auto session = move( _closeSession );
		if( !session )
			return;
		_writeQueue.clear();//nothing queued can go out now - drop the frames, and the session refs they hold, here rather than at destruction.
		if( !_open || _transportClosed ){//handshake never completed, or the deadline already tore the transport down: async_close is invalid either way.
			if( !_transportClosed )
				beast::get_lowest_layer( _ws ).close();//abort the pending accept/connect at the transport; its completion eats the error.
			if( _closeDeadline )
				_closeDeadline->cancel();
			OnClosed( move(session), _transportClosed ? beast::error::timeout : beast::error_code{} );
			return;
		}
		_ws.async_close( _closeCode, net::bind_executor(_strand, [this, self=this->shared_from_this(), session=move(session)]( beast::error_code ec )mutable{
			if( _closeDeadline )
				_closeDeadline->cancel();//completed in time - stand the deadline down before it tears the transport out from under a finished close.
			OnClosed( move(session), ec );
		}) );
	}
#undef $
}

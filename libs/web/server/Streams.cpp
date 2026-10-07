#include "Streams.h"
#include <jde/web/server/IWebsocketSession.h>
#include <jde/web/server/HttpRequest.h>
#include <jde/web/server/Server.h>

namespace Jde::Web::Server{
	α IRestStream::OnWrite( beast::error_code ec, uint bytes_transferred )ι->void{
		boost::ignore_unused( bytes_transferred );
		if( ec )
			CodeException{ static_cast<std::error_code>(ec), ELogTags::HttpClientWrite, ErrorSeverity(ec, EErrorRole::HttpServerWrite) };
	}

#define $ template<class TStream> auto RestStream<TStream>
	//No keep-alive (web-refactor B3):  RunSession hands the socket over for one request and reads no further one, so the response
	//says so - advertising keep-alive left a browser to reuse a connection that was already closing.
	$::AsyncWrite( http::response<http::string_body>&& res )ι->void{
		res.keep_alive( false );
		beast::async_write( _stream, http::message_generator{move(res)}, beast::bind_front_handler(&RestStream::OnWrite, std::static_pointer_cast<RestStream>(shared_from_this())) );
	}
	$::OnWrite( beast::error_code ec, uint bytes_transferred )ι->void{
		IRestStream::OnWrite( ec, bytes_transferred );
		if constexpr( std::is_same_v<TStream, beast::ssl_stream<StreamType>> ){
			beast::get_lowest_layer( _stream ).expires_after( 5s );//a peer that never answers the close_notify must not hold the socket.
			_stream.async_shutdown( [self=std::static_pointer_cast<RestStream>(shared_from_this())]( beast::error_code ec ){
				if( ec )
					TRACET( ELogTags::HttpServerWrite, "shutdown: {}", ec.message() );
			} );
		}
		else{
			beast::error_code shutdownEc;
			_stream.socket().shutdown( tcp::socket::shutdown_send, shutdownEc );
		}
	}

	$::CreateSocketStream( beast::flat_buffer&& buffer )ι->sp<ISocketStream>{
		return ms<SocketStream<websocket::stream<TStream>>>( move(_stream), move(buffer) );
	}
#undef $

	template<class TStream>
	SocketStream<TStream>::SocketStream( typename TStream::next_layer_type&& next, beast::flat_buffer&& buffer )ι:
		Core{ net::make_strand(executor_type{next.get_executor()}), move(next) },
		_buffer{ move(buffer) }
	{
		_ws.binary( true );
	}

#define $ template<class TStream> auto SocketStream<TStream>
	$::DoAccept( TRequestType req, sp<IWebsocketSession> session )ι->void{
		net::dispatch( _strand, [this, self=this->shared_from_this(), req=move(req), session=move(session)]()mutable{
			if( this->IsClosing() )
				return;
			_ws.set_option( websocket::stream_base::timeout::suggested(beast::role_type::server) );
			//#16: Beast defaults this to 16 MB, so the socket accepted a query three orders of magnitude past what the http
			//path allows - and the parser recursed through all of it on an 8 MB io thread stack.
			//web-review3 #14: its own limit now, not /graphql's.  A socket message is not an http body - sharing the body cap
			//made any RemoteLog backlog past 10 KB permanently undeliverable.
			_ws.read_message_max( Server::SocketMessageMax() );
			_ws.set_option( websocket::stream_base::decorator( []( websocket::response_type& res ){
				res.set( http::field::server, ServerVersion(IsSsl) );
			}) );
			_ws.async_accept( req, net::bind_executor(_strand, [this, self, session]( beast::error_code ec ){
				_open = !ec;
				session->OnAccept( ec );
			}) );
		});
	}

	$::DoRead( sp<IWebsocketSession> session )ι->void{
		net::dispatch( _strand, [this, self=this->shared_from_this(), session=move(session)]()mutable{
			if( this->IsClosing() )
				return;
			_ws.async_read( _buffer, net::bind_executor(_strand, [this, self, session]( beast::error_code ec, uint /*c*/ )mutable{
				if( ec ){
					const auto level = ErrorSeverity( ec, EErrorRole::SocketServerRead );
					if( ec == websocket::error::closed ){
						CodeException{ static_cast<std::error_code>(ec), ELogTags::SocketClientRead, Ƒ("[{:x}]Server::DoRead", session->Id()), level };
						session->OnClose();
					}else
						session->OnDisconnect( CodeException{static_cast<std::error_code>(ec), ELogTags::SocketClientRead, Ƒ("[{:x}]Server::DoRead", session->Id()), level} );
					return;
				}
				session->OnRead( (char*)_buffer.data().data(), _buffer.size() );
				_buffer.clear();
				session->DoRead();
			}) );
		});
	}

	$::OnWriteError( sp<IWebsocketSession> session, beast::error_code ec )ι->void{
		DBGT( ELogTags::SocketClientWrite | ELogTags::ExternalLogger, "({})Error writing to Session:  '{}'", ec.value(), boost::diagnostic_information(ec) );
		CodeException{ ec, ELogTags::SocketClientRead };
		Close( move(session) );//no-op if a close is already under way - the core finishes that one, now that nothing is outstanding.
	}

	$::OnClosed( sp<IWebsocketSession> session, beast::error_code ec )ι->void{
		if( ec && ec!=beast::error::timeout )//timeout: the close deadline - the write it aborted has already logged.
			CodeException{ static_cast<std::error_code>(ec), ELogTags::SocketClientRead };
		session->OnClose();
	}
#undef $
	template struct SocketStream<websocket::stream<StreamType>>;
	template struct SocketStream<websocket::stream<beast::ssl_stream<StreamType>>>;
	template struct RestStream<StreamType>;
	template struct RestStream<beast::ssl_stream<StreamType>>;
}

#pragma once
#include <boost/asio/post.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <jde/web/SocketCore.h>
#include <jde/web/server/usings.h>

namespace Jde::Web::Server{
	struct IWebsocketSession; struct ISocketStream;
	//Non-template handle for the http side - RestStream<TStream> owns the socket once RunSession hands it over, and turns it into the websocket on upgrade.
	struct IRestStream: std::enable_shared_from_this<IRestStream>{
		virtual ~IRestStream()=default;
		β AsyncWrite( http::response<http::string_body>&& res )ι->void=0;//the connection's last response:  sent with Connection: close, then the socket is shut down.
		β CreateSocketStream( beast::flat_buffer&& buffer )ι->sp<ISocketStream> = 0;
	protected:
		α OnWrite( beast::error_code ec, uint bytes_transferred )ι->void;
	};

	//TStream: StreamType | beast::ssl_stream<StreamType>.  Bodies live in Streams.cpp, explicitly instantiated for both.
	template<class TStream>
	struct RestStream final: IRestStream{
		RestStream( TStream&& stream )ι:_stream{ move(stream) }{}
		α AsyncWrite( http::response<http::string_body>&& res )ι->void override;
		α CreateSocketStream( beast::flat_buffer&& buffer )ι->sp<ISocketStream> override;
	private:
		α OnWrite( beast::error_code ec, uint bytes_transferred )ι->void;
		TStream _stream;
	};

	//Non-template handle held by IWebsocketSession; SocketStream<TStream> supplies the beast ops.
	struct ISocketStream{
		virtual ~ISocketStream()=default;
		β Write( string&& buffer, sp<IWebsocketSession> session )ι->void=0;
		β DoAccept( TRequestType request, sp<IWebsocketSession> session )ι->void=0;
		β DoRead( sp<IWebsocketSession> session )ι->void=0;
		β Close( sp<IWebsocketSession> session )ι->void=0;
		β Strand()ι->const net::strand<executor_type>& =0;
	};

	//TStream: websocket::stream<StreamType> | websocket::stream<beast::ssl_stream<StreamType>>.  Bodies live in Streams.cpp, explicitly instantiated for both.
	//The write queue & close state machine are SocketCore's, shared with the client; this adds the accept and the read loop.
	template<class TStream>
	struct SocketStream final: ISocketStream, SocketCore<TStream,IWebsocketSession>{
		using Core = SocketCore<TStream,IWebsocketSession>;
		static constexpr bool IsSsl = std::is_same_v<TStream, websocket::stream<beast::ssl_stream<StreamType>>>;
		SocketStream( typename TStream::next_layer_type&& next, beast::flat_buffer&& buffer )ι;//next = RestStream<>::_stream
		α Write( string&& buffer, sp<IWebsocketSession> session )ι->void override{ Core::Write( move(buffer), move(session) ); }
		α DoAccept( TRequestType request, sp<IWebsocketSession> session )ι->void override;
		α DoRead( sp<IWebsocketSession> session )ι->void override;
		α Close( sp<IWebsocketSession> session )ι->void override{ Core::Close( move(session) ); }
		α Strand()ι->const net::strand<executor_type>& override{ return _strand; }
	private:
		α OnClosed( sp<IWebsocketSession> session, beast::error_code ec )ι->void override;
		α OnWriteError( sp<IWebsocketSession> session, beast::error_code ec )ι->void override;
		using Core::_strand; using Core::_ws; using Core::_open;
		beast::flat_buffer _buffer;//strand-confined.
	};
}

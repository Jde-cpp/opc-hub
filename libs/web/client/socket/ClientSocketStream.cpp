#include <jde/web/client/socket/ClientSocketStream.h>
#include <jde/web/SocketCore.h>
#include <jde/web/client/ClientSsl.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include "webClientUtils.h"

#define let const auto
namespace Jde::Web::Client{
	constexpr ELogTags _connectTag{ ELogTags::Socket | ELogTags::Client };
	static const string _userAgent{ Ƒ("({})Jde.Web.Client - {}", Process::ProductVersion, BOOST_BEAST_VERSION) };
	static const string _sslUserAgent{ Ƒ("({})Jde.Web.Client SSL - {}", Process::ProductVersion, BOOST_BEAST_VERSION) };
	α UserAgent( bool ssl )ι->str{ return ssl ? _sslUserAgent : _userAgent; }

	//TWs: websocket::stream<beast::tcp_stream> | SslSocketStream, explicitly instantiated for both below.
	//The write queue & close state machine are SocketCore's, shared with the server; this adds connect/handshake and the read.
	template<class TWs>
	struct ClientSocketStream final: IClientSocketStream, SocketCore<TWs,IClientSocketSession>{
		using Core = SocketCore<TWs,IClientSocketSession>;
		//_ws lives on the strand, so the connect/handshake/read completions run there unbound.
		ClientSocketStream( typename Core::StrandType strand, auto&... ctx )ι:Core{ strand, typename Core::StrandType{strand}, ctx... }{ _ws.binary( true ); }

		α OnResolve( tcp::resolver::results_type results, sp<IClientSocketSession> session )ι->void override;
		α OnConnect( tcp::resolver::results_type::endpoint_type ep, string& host, sp<IClientSocketSession> session )ι->void override;
		α AfterHandshake( const string& host, sp<IClientSocketSession> session )ι->void override;
		α AsyncRead( sp<IClientSocketSession> session )ι->void override;
		α AsyncWrite( string buffer, sp<IClientSocketSession> session )ι->void override{ Core::Write( move(buffer), move(session) ); }
		α Close( sp<IClientSocketSession> session, bool terminate, SL sl )ι->void override;
		α ReadBuffer()ι->std::span<uint8_t> override{ return std::span<uint8_t>{(uint8_t*)_buffer.data().data(), _buffer.size()}; }
		α IsSsl()Ι->bool override{ return _isSsl; }
		α IsClosing()Ι->bool override{ return Core::IsClosing(); }
	private:
		static constexpr bool _isSsl = std::is_same_v<TWs,SslSocketStream>;
		α OnClosed( sp<IClientSocketSession> session, beast::error_code ec )ι->void override;
		α OnWriteError( sp<IClientSocketSession> session, beast::error_code ec )ι->void override;
		using Core::_ws; using Core::_open;
		beast::flat_buffer _buffer;
	};

	α IClientSocketStream::Create( net::io_context& ioc, optional<ssl::context>& ctx )ι->sp<IClientSocketStream>{
		auto strand = net::make_strand( ioc );
		if( ctx )
			return ms<ClientSocketStream<SslSocketStream>>( move(strand), *ctx );
		return ms<ClientSocketStream<websocket::stream<beast::tcp_stream>>>( move(strand) );
	}

#define $ template<class TWs> auto ClientSocketStream<TWs>
	$::OnResolve( tcp::resolver::results_type results, sp<IClientSocketSession> session )ι->void{
		auto endpoints = PreferV4( results );// async_connect walks endpoints serially; try IPv4 first to avoid dead-IPv6 connect stalls (see PreferV4).
		beast::get_lowest_layer( _ws ).expires_after( std::chrono::seconds(30) );
		beast::get_lowest_layer( _ws ).async_connect( endpoints, beast::bind_front_handler(&IClientSocketSession::OnConnect, session) );// Make the connection on the IP address we get from a lookup
	}

	$::OnConnect( tcp::resolver::results_type::endpoint_type ep, string& host, sp<IClientSocketSession> session )ι->void{
		if constexpr( _isSsl ){
			beast::get_lowest_layer( _ws ).expires_after( std::chrono::seconds(30) );// Set a timeout on the operation
			if( !SSL_set_tlsext_host_name(_ws.next_layer().native_handle(), host.c_str()) ){// Set SNI Hostname (many hosts need this to handshake successfully)
				let ec = beast::error_code( static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() );
				CodeException{ static_cast<std::error_code>(ec), ELogTags::SocketClientRead };
				return;
			}
			Ssl::SetVerifyHost( _ws.next_layer(), host );//C1: bind the peer's certificate to the host we dialled, not just to a trusted anchor.
			host += ':' + std::to_string( ep.port() ); // Update the _host string. This will provide the value of the Host HTTP header during the WebSocket handshake. See https://tools.ietf.org/html/rfc7230#section-5.4
			_ws.next_layer().async_handshake( ssl::stream_base::client, beast::bind_front_handler( &IClientSocketSession::OnSslHandshake, session) );
		}
		else{
			host += ':' + std::to_string( ep.port() );// Update the host string. This will provide the value of the Host HTTP header during the WebSocket handshake.  See https://tools.ietf.org/html/rfc7230#section-5.4
			AfterHandshake( host, session );
		}
	}
	$::AfterHandshake( const string& host, sp<IClientSocketSession> session )ι->void{
		beast::get_lowest_layer( _ws ).expires_never();// Turn off the timeout on the tcp_stream, because the websocket stream has its own timeout system.
		_ws.set_option( websocket::stream_base::timeout::suggested(beast::role_type::client) );// Set suggested timeout settings for the websocket
		string userAgent = UserAgent( _isSsl );
		_ws.set_option(websocket::stream_base::decorator( [userAgent](websocket::request_type& req){// Set a decorator to change the User-Agent of the handshake
			req.set( http::field::user_agent, userAgent );
		}));
		_ws.async_handshake( host, session->_target, [this, self=this->shared_from_this(), session]( beast::error_code ec ){// Perform the websocket handshake - the session's target, "/" unless it asked for a path.
			_open = !ec;
			session->OnHandshake( ec );
		} );
	}

	$::AsyncRead( sp<IClientSocketSession> session )ι->void{
		_buffer.consume( _buffer.size() );
		_ws.async_read( _buffer, beast::bind_front_handler(&IClientSocketSession::OnRead, session) );
	}

	$::Close( sp<IClientSocketSession> session, bool terminate, SL )ι->void{
		if( !IsClosing() )
			DBGT( _connectTag, "[{}]Client::Close: {}", hex(session->Id()), session->Host() );
		Core::Close( move(session), terminate ? websocket::close_code::going_away : websocket::close_code::normal );
	}

	$::OnWriteError( sp<IClientSocketSession> /*session*/, beast::error_code ec )ι->void{
		CodeException{ static_cast<std::error_code>(ec), ELogTags::SocketClientWrite };//TODO look at returning an error to caller.
	}

	//The session's OnClose is what completes CloseClientSocketSessionAwait.
	$::OnClosed( sp<IClientSocketSession> session, beast::error_code ec )ι->void{
		session->OnClose( ec );
	}
#undef $
	template struct ClientSocketStream<websocket::stream<beast::tcp_stream>>;
	template struct ClientSocketStream<SslSocketStream>;
}

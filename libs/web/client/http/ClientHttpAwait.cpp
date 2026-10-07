#include <jde/web/client/http/ClientHttpAwait.h>
#include <thread>
#include <absl/strings/match.h>
#include <jde/fwk/process/execution.h>
#include <jde/web/client/client.h>
#include <jde/web/client/ClientSsl.h>
#include <jde/web/client/http/ClientHttpException.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include "webClientUtils.h"

#define let const auto

namespace Jde::Web{
	Duration _handshakeTimeout{};
	Ω handshakeTimeout()ι{
		if( _handshakeTimeout==Duration::zero() )
			_handshakeTimeout = Settings::FindDuration( "/web/client/timeoutHandshake" ).value_or( std::chrono::seconds(30) );
		return _handshakeTimeout;
	}
	Duration _timeout{};
	Ω timeout()ι{
		if( _timeout==Duration::zero() )
			_timeout = Settings::FindDuration( "/web/client/timeout" ).value_or( std::chrono::seconds(30) );
		return _timeout;
	}
	constexpr ELogTags _tags{ ELogTags::HttpClientWrite };
	constexpr auto _token{ net::as_tuple(net::use_awaitable) };

	//Process shutdown waits out the requests still in flight, their connections' shutdowns included.
	static atomic<uint> _inFlight{};
	struct InFlight final{
		InFlight()ι{ ++_inFlight; }
		InFlight( const InFlight& )ι{ ++_inFlight; }
		~InFlight(){ --_inFlight; }
	};
	struct RequestShutdown final : IShutdown{
		RequestShutdown()ι{ Execution::AddShutdown(this); }
		α Shutdown( bool terminate, SL )ι->void override{
			while( !terminate && _inFlight )
				std::this_thread::sleep_for( 100ms );
		}
	};
	RequestShutdown _requestShutdown;

	using PlainStream = beast::tcp_stream;
	using SslStream = beast::ssl_stream<beast::tcp_stream>;

	Ω makeRequest( str host, str target, string&& body, const Client::HttpAwaitArgs& args )ι->http::request<http::string_body>{
		ASSERT( args.Verb.has_value() );
		constexpr int version{ 11 };
		http::request<http::string_body> req{ *args.Verb, target, version };
		req.set( http::field::host, host );
		req.set( http::field::user_agent, Client::UserAgent(args.IsSsl) );
		if( args.ContentType.size() )
			req.set( http::field::content_type, args.ContentType );
		req.set( http::field::accept_encoding, "gzip" );
		if( args.Authorization.size() )
			req.set( http::field::authorization, args.Authorization );
		if( args.Origin.size() )
			req.set( http::field::origin, args.Origin );

		req.body() = move( body );
		req.prepare_payload();
		return req;
	}

	Ŧ makeStream( const net::any_io_executor& strand, str host )ε->T{
		using namespace Client;
		if constexpr( std::is_same_v<T,PlainStream> )
			return T{ strand };
		else{
			T stream{ strand, Ssl::Context() };
			if( !SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()) )
				throw ClientHttpException{ beast::error_code{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()}, host };
			Ssl::SetVerifyHost( stream, host );//C1: the context brings the trust anchors, this binds the answer to the host we dialled.
			return stream;
		}
	}

	Ω shutdown( SslStream stream, InFlight )ι->net::awaitable<void>{
		beast::get_lowest_layer( stream ).expires_after( handshakeTimeout() );
		let [ec] = co_await stream.async_shutdown( _token );
		if( ec )
			LOG( ErrorSeverity(ec, EErrorRole::HttpClientShutdown), ELogTags::Shutdown | ELogTags::Http | ELogTags::Client, "shutdown: {}", ec.message() );
	}

	//One exchange on a connection of its own:  resolve, connect, handshake if TLS, write, read.  T: PlainStream | SslStream.
	Ŧ send( str host, PortType port, const http::request<http::string_body>& req, SL sl )ε->net::awaitable<Client::ClientHttpRes>{
		using namespace Client;
		let strand = co_await net::this_coro::executor;
		auto stream = makeStream<T>( strand, host );
		tcp::resolver resolver{ strand };
		auto [ec, resolved] = co_await resolver.async_resolve( host, std::to_string(port), _token );
		if( ec )
			throw ClientHttpException{ ec };
		beast::get_lowest_layer( stream ).expires_after( handshakeTimeout() );
		std::tie( ec, std::ignore ) = co_await beast::get_lowest_layer( stream ).async_connect( PreferV4(resolved), _token );// async_connect walks endpoints serially; try IPv4 first to avoid dead-IPv6 connect stalls (see PreferV4).
		if( ec )
			throw ClientHttpException{ ec };
		if constexpr( std::is_same_v<T,SslStream> ){
			beast::get_lowest_layer( stream ).expires_after( handshakeTimeout() );
			std::tie( ec ) = co_await stream.async_handshake( ssl::stream_base::client, _token );
			if( ec )
				throw ClientHttpException{ ec };
		}

		let target = string{ req.target() };
		LOGSL( ELogLevel::Trace, sl, _tags, "{}:{}{} - {}", host, port, target, req.body().substr(0, MaxLogLength()) );
		beast::get_lowest_layer( stream ).expires_after( timeout() );
		beast::flat_buffer buffer;
		http::response<http::string_body> res;
		std::tie( ec, std::ignore ) = co_await http::async_write( stream, req, _token );
		if( !ec )
			std::tie( ec, std::ignore ) = co_await http::async_read( stream, buffer, res, _token );

		if constexpr( std::is_same_v<T,SslStream> )
			net::co_spawn( strand, shutdown(move(stream), {}), net::detached );//started, not awaited: the response does not wait on the peer's close_notify.
		else{
			beast::error_code shutdownEc;
			stream.socket().shutdown( tcp::socket::shutdown_both, shutdownEc );
			if( shutdownEc )
				LOG( ErrorSeverity(shutdownEc, EErrorRole::HttpClientShutdown), ELogTags::Shutdown | ELogTags::Http | ELogTags::Client, "shutdown: {}", shutdownEc.message() );
		}
		if( ec )
			throw ClientHttpException{ ec, host, target, port };
		ClientHttpRes y{ res };
		LOGSL( ELogLevel::Trace, sl, ELogTags::HttpClientRead, "{}:{}{} - {}", host, port, target, y.Body().substr(0, MaxLogLength()) );
		co_return y;
	}

	//The request and the redirects that follow it.  Spawned on a strand of its own.
	Ω request( string host, PortType port, string target, string body, Client::HttpAwaitArgs args, SL sl, InFlight={}/*a parameter: counted from the call, not from when the strand gets to it.*/ )ε->net::awaitable<Client::ClientHttpRes>{
		using namespace Client;
		for( ;; ){
			auto req = makeRequest( host, target, move(body), args );
			ClientHttpRes res;
			for( bool retried{};; retried=true ){
				try{
					res = args.IsSsl ? co_await send<SslStream>( host, port, req, sl ) : co_await send<PlainStream>( host, port, req, sl );
					break;
				}
				catch( ClientHttpException& e ){
					if( retried || !e.SslStreamTruncated() )//a peer that dropped the TLS stream without a close_notify gets one more attempt.
						throw;
				}
			}
			if( !res.IsRedirect() || !args.AllowRedirects ){
				THROW_IFX( res.IsError(), ClientHttpResException(move(res), Ƒ("{}:{}{}", host, port, target), sl) );
				co_return res;
			}

			if( !args.Redirects )//budget spent - a server redirecting to itself would otherwise loop forever.
				throw Exception{ sl, {ELogTags::HttpClientRead}, "Too many redirects from {}:{}{} - last Location '{}'.", host, port, target, res[http::field::location] };
			auto [toHost,toTarget,toPort,toSsl] = res.RedirectVariables();
			--args.Redirects;
			let fromSsl = args.IsSsl;
			if( toHost.empty() ){//relative Location - reuse the original host, port & scheme.
				toHost = host;
				toPort = port;
			}
			else
				args.IsSsl = toSsl;
			DBG( "redirecting from {}{} to {}", host, target, res[http::field::location] );
			//an Authorization header is a credential for the host it was issued to; a redirect elsewhere must not carry it.
			//Whoever controls the Location header would otherwise be handed the caller's session id or bearer token.
			let downgrade = fromSsl && !args.IsSsl;
			if( args.Authorization.size() && (downgrade || !absl::EqualsIgnoreCase(toHost, host)) ){
				DBG( "dropping Authorization: redirect leaves {}{} for {}{}", fromSsl ? "https://" : "http://", host, args.IsSsl ? "https://" : "http://", toHost );
				args.Authorization.clear();
			}
			body = move( req.body() );
			host = move( toHost );
			target = move( toTarget );
			port = toPort;
		}
	}

namespace Client{
	ClientHttpAwait::ClientHttpAwait( string host, string target, PortType port, HttpAwaitArgs args, SL sl )ι:
		base{ sl },
		_host{ move(host) }, _target{ move(target) }, _port{ port }, _args{ move(args) },
		_ioContext{ Executor() }{
		if( _args.Verb==http::verb::unknown )
			_args.Verb=http::verb::get;
	}
	ClientHttpAwait::ClientHttpAwait( string host, string target, string body, PortType port, HttpAwaitArgs args, SL sl )ι:
		base{ sl },
		_host{ move(host) }, _target{ move(target) }, _body{ move(body) }, _port{ port }, _args{ move(args) },
		_ioContext{ Executor() }{
		if( _args.Verb==http::verb::unknown )
			_args.Verb=http::verb::post;
	}

	α ClientHttpAwait::Suspend()ι->void{
		net::co_spawn( net::make_strand(*_ioContext), request(_host, _port, _target, _body, _args, _sl), [this]( std::exception_ptr exp, ClientHttpRes res )ι{
			if( !exp ){
				Resume( move(res) );
				return;
			}
			try{
				std::rethrow_exception( exp );
			}
			catch( runtime_error& e ){//ResumeExp takes runtime_error - a Jde/ClientHttp exception keeps its type through it.
				ResumeExp( move(e) );
			}
			catch( std::exception& e ){//a logic_error must still fail the caller rather than hang it.
				ResumeExp( Exception{_sl, ELogLevel::Critical, "{}", e.what()} );
			}
		});
	}

	α ClientHttpAwait::await_resume()ε->ClientHttpRes{
		if( !Promise() )
			throw Exception( "Executor down." );
		return base::await_resume();
	}
}}
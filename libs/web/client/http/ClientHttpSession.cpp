#include <exception>
#include <absl/strings/match.h>
#include <jde/web/client/http/ClientHttpSession.h>
#include <jde/web/client/ClientSsl.h>
#include <jde/web/client/http/ClientHttpRes.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include <jde/fwk/process/execution.h>

#define let const auto

namespace Jde::Web::Client{
	Duration _handshakeTimeout{};
	α handshakeTimeout()ι{
		if( _handshakeTimeout==Duration::zero() )
			_handshakeTimeout = Settings::FindDuration( "/web/client/timeoutHandshake" ).value_or( std::chrono::seconds(30) );
		return _handshakeTimeout;
	}
	Duration _timeout{};
	α Timeout()ι{
		if( _timeout==Duration::zero() )
			_timeout = Settings::FindDuration( "/web/client/timeout" ).value_or( std::chrono::seconds(30) );
		return _timeout;
	}
	ELogTags _tags{ ELogTags::HttpClientWrite };
	static string _userAgent{ Ƒ("({})Jde.Web.Client - {}", Process::ProductVersion, BOOST_BEAST_VERSION) };

	ClientHttpSession::ClientHttpSession( str host, PortType port, net::any_io_executor strand, bool isPlain, bool log, SL sl )ε:
		Host{ host }, Port{ port }, IsSsl{ false }, _log{ log }, _resolver{ strand },
		_stream{ beast::tcp_stream{strand} },
		_sl{ sl }{
		ASSERT( isPlain );
		_isRunning.test_and_set();
	}

	ClientHttpSession::ClientHttpSession( str host, PortType port, net::any_io_executor strand, SL sl )ε:
		Host{ host }, Port{ port }, IsSsl{ true }, _log{ true }, _resolver{ strand },
		_stream{ beast::ssl_stream<beast::tcp_stream>{strand, Ssl::Context()} },
		_sl{ sl }{
		_stream.SetSslTlsExtHostName( Host );
		_stream.SetVerifyHost( Host );//C1: the context brings the trust anchors, this binds the answer to the host we dialled.
		_isRunning.test_and_set();
	}

	struct ConnectAwait final : VoidAwait{
		ConnectAwait( tcp::resolver::results_type&& resolvedResults, sp<ClientHttpSession> session, SRCE )ι:
			VoidAwait{ sl },
			_resolvedResults{ move(resolvedResults) },
			_session{ session }
		{}
		α Suspend()ι->void override{
			_session->Stream().expires_after( handshakeTimeout() );
			_session->Stream().async_connect( _resolvedResults, beast::bind_front_handler(&ConnectAwait::OnConnect, this) );
		}
		α OnConnect( beast::error_code ec, tcp::resolver::results_type::endpoint_type )->void{
			if( ec )
				ResumeExp( ClientHttpException{ec} );
			else
				Resume();
		}
		tcp::resolver::results_type _resolvedResults;
		sp<ClientHttpSession> _session;
	};

	struct HandshakeAwait final : VoidAwait{
		HandshakeAwait( sp<ClientHttpSession> session, SRCE )ι:VoidAwait{ sl }, _session{ session }{}
		α Suspend()ι->void override{
			_session->Stream().expires_after( handshakeTimeout() );
			_session->Stream().async_handshake( beast::bind_front_handler(&HandshakeAwait::OnHandshake, this) );
		}
		α OnHandshake( beast::error_code ec )->void{
			if( ec )
				ResumeExp( ClientHttpException{ec} );
			else
				Resume();
		}
		sp<ClientHttpSession> _session;
	};

	struct ResolveAwait final : VoidAwait{
		ResolveAwait( sp<ClientHttpSession> session, SRCE )ι:VoidAwait{ sl }, _session{ session }{}
		α Suspend()ι->void override{
			_session->Resolver().async_resolve( _session->Host, std::to_string(_session->Port), beast::bind_front_handler(&ResolveAwait::OnResolve, this) );
		}
		α OnResolve( beast::error_code ec, tcp::resolver::results_type results )->void{
			if( ec )
				ResumeExp( ClientHttpException{ec} );
			else{
				Results = move( results );
				Resume();
			}
		}
		tcp::resolver::results_type Results;
		sp<ClientHttpSession> _session;
	};

	struct MakeConnectionAwait final : VoidAwait{
		MakeConnectionAwait( sp<ClientHttpSession> session, SRCE )ι:VoidAwait{ sl }, _session{ session }{}
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->ConnectAwait::Task{
			try{
				ResolveAwait resolve{ _session };//named: the await has to outlive the suspension it carries its result across.
				co_await resolve;
				co_await ConnectAwait{ move(resolve.Results), _session };
				if( _session->IsSsl )
					co_await HandshakeAwait{ _session };
				Resume();
			}
			catch( runtime_error& e ){
				ResumeExp( move(e) );
			}
		}
		sp<ClientHttpSession> _session;
	};

	struct AsyncWriteAwait final : TAwait<ClientHttpRes>{
		using base=TAwait<ClientHttpRes>;
		AsyncWriteAwait( http::request<http::string_body> req, const HttpAwaitArgs& args, sp<ClientHttpSession> session, SRCE )ι:base{ sl }, _req{ move(req) }, _args{ args }, _session{ session }{}
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->void{
			_session->Stream().expires_after( Timeout() );
			_session->Stream().async_write( _req, beast::bind_front_handler(&AsyncWriteAwait::OnWrite, this) );
		}
		α OnWrite( beast::error_code ec, std::size_t bytes_transferred )->void{
      boost::ignore_unused( bytes_transferred );
			if( ec )
				ResumeExp( ClientHttpException{ec, _session, &_req} );
			else
				_session->Stream().async_read( _res, _buffer, beast::bind_front_handler(&AsyncWriteAwait::OnRead, this) );
		}
		α OnRead( beast::error_code ec, std::size_t bytes_transferred )ι->ClientHttpAwait::Task{
      boost::ignore_unused( bytes_transferred );
			if( ec ){
				ResumeExp( ClientHttpException{ec, _session, &_req} );
				co_return;
			}
			try{
				ClientHttpRes res{ move(_res) };
				if( res.IsRedirect() && _args.AllowRedirects ){
					if( !_args.Redirects ){//budget spent - a server redirecting to itself would otherwise loop forever.
						ResumeExp( Exception{_sl, {ELogTags::HttpClientRead}, "Too many redirects from {}:{}{} - last Location '{}'.", _session->Host, _session->Port, string{_req.target()}, res[http::field::location]} );
						co_return;
					}
					auto [host,target,port,isSsl] = res.RedirectVariables();
					auto args = _args;
					--args.Redirects;
					if( host.empty() ){//relative Location - reuse the original host, port & scheme.
						host = _session->Host;
						port = _session->Port;
					}
					else
						args.IsSsl = isSsl;
					DBG( "redirecting from {}{} to {}", _session->Host, _req.target(), res[http::field::location] );
					//an Authorization header is a credential for the host it was issued to; a redirect elsewhere must not carry it.
					//Whoever controls the Location header would otherwise be handed the caller's session id or bearer token.
					let downgrade = _args.IsSsl && !args.IsSsl;
					if( args.Authorization.size() && (downgrade || !absl::EqualsIgnoreCase(host, _session->Host)) ){
						DBG( "dropping Authorization: redirect leaves {}{} for {}{}", _args.IsSsl ? "https://" : "http://", _session->Host, args.IsSsl ? "https://" : "http://", host );
						args.Authorization.clear();
					}
					res = co_await ClientHttpAwait{ host, target, _req.body(), port, args, _sl };
				}
				Resume( move(res) );
			}
			catch( runtime_error& e ){//IAwait::ResumeExp takes runtime_error - a Jde/ClientHttp exception keeps its type through it.
				ResumeExp( move(e) );
			}
		}
	private:
		http::request<http::string_body> _req;
		const HttpAwaitArgs& _args;
		sp<ClientHttpSession> _session;
		beast::flat_buffer _buffer;
		http::response<http::string_body> _res;
	};

	struct ShutdownAwait final : VoidAwait{
		ShutdownAwait( sp<ClientHttpSession> session, SRCE )ι:VoidAwait{ sl }, _session{ session }{}
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->void{
			_session->Stream().expires_after( handshakeTimeout() );
      _session->Stream().async_shutdown( [this](beast::error_code ec){ShutdownAwait::OnShutdown(ec);} );
		}
		α OnShutdown( beast::error_code ec )->void{
			if( ec && (!_session->IsSsl || (_session->IsSsl && ec !=net::error::eof)) )
				TRACET( ELogTags::Shutdown | ELogTags::Http | ELogTags::Client, "shutdown: {}", ec.message() );
			Resume();
		}
	private:
		sp<ClientHttpSession> _session;
	};

  α ClientHttpSession::Send( string target, string body, const HttpAwaitArgs& args, ClientHttpAwaitSingle::Handle h )ι->VoidTask{
		try{
			if( !_isConnected ){
				co_await MakeConnectionAwait{ shared_from_this() };
				_isConnected = true;
			}
			Write( move(target), move(body), args, h );
		}
		catch( runtime_error& e ){
			SetIsRunning( false );
			RemoveHttpSession( shared_from_this() );
			h.promise().ResumeExp( move(e), h );
		}
  }

	α ClientHttpSession::Write( string target, string body, const HttpAwaitArgs& args, ClientHttpAwaitSingle::Handle h )ι->AsyncWriteAwait::Task{
		ASSERT( args.Verb.has_value() );
		constexpr int version{ 11 };
		if( _log )
			LOGSL( ELogLevel::Trace, _sl, _tags, "{}:{}{} - {}", Host, Port, target, body.substr(0, Client::MaxLogLength()) );
		http::request<http::string_body> req{ *args.Verb, target, version };
		req.set( http::field::host, Host );
		req.set( http::field::user_agent, _userAgent );
		if( args.ContentType.size() )
			req.set( http::field::content_type, args.ContentType );
		req.set( http::field::accept_encoding, "gzip" );
		if( args.Authorization.size() )
			req.set( http::field::authorization, args.Authorization );
		if( args.Origin.size() )
			req.set( http::field::origin, args.Origin );

		req.body() = move( body );
		req.prepare_payload();
		try{
			auto res = co_await AsyncWriteAwait{ move(req), args, shared_from_this() };
			if( _log )
				LOGSL( ELogLevel::Trace, _sl, ELogTags::HttpClientRead, "{}:{}{} - {}", Host, Port, target, res.Body().substr(0, Client::MaxLogLength()) );
			h.promise().SetValue( move(res) );
		}
		catch( runtime_error& e ){
			h.promise().SetExp( move(e) );
		}
		Close();
		h.resume();
	}

	α ClientHttpSession::Close()ε->VoidTask{
		let self = shared_from_this();
		co_await ShutdownAwait{ self };
		RemoveHttpSession( self );//TODO implement keep-alive
		SetIsRunning( false );
	}
}
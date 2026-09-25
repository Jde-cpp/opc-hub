#include "ServerImpl.h"
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/process/execution.h>
#include "jde/db/DBException.h"
#include "jde/fwk/exceptions/Exception.h"
#include <jde/ql/ql.h>
#include <jde/ql/QLAwait.h>
#include <jde/access/AccessException.h>
#include <jde/web/client/ClientSsl.h>
#include <jde/web/server/IHttpRequestAwait.h>
#include <jde/web/server/IRequestHandler.h>
#include <jde/web/server/IWebsocketSession.h>
#include <jde/web/server/RestException.h>
#include <jde/app/IApp.h>
#define let const auto

namespace Jde::Web{
namespace Server{
	Ω detectSession( StreamType stream, tcp::endpoint userEndpoint, sp<net::cancellation_signal> cancel, Server::IRequestHandler* handler )ι->net::awaitable<void, executor_type>{
		beast::flat_buffer buffer;
		stream.expires_after( std::chrono::seconds(30) );// Set the timeout.
		auto [ec, isSsl] = co_await beast::async_detect_ssl( stream, buffer );// on_run
		if( ec ){
			CodeException{ ec, ELogTags::Server | ELogTags::Http, ELogLevel::Debug };
			co_return;
		}
		let index = Internal::NextConnectionIndex();
		if( isSsl ){
			beast::ssl_stream<StreamType> ssl_stream{ move(stream), handler->Context() };
			auto [ec, bytes_used] = co_await ssl_stream.async_handshake( ssl::stream_base::server, buffer.data() );
			if( ec ){
				CodeException{ ec, ELogTags::Server | ELogTags::Http, ELogLevel::Warning };
				co_return;
			}

			buffer.consume( bytes_used );
			co_await RunSession( ssl_stream, buffer, move(userEndpoint), true, index, cancel, handler );
		}
		else
			co_await RunSession( stream, buffer, move(userEndpoint), false, index, cancel, handler );
	}

	Ω send( HttpRequest&& req, sp<IRestStream> stream, jvalue j, sv contentType={}, SRCE )ι->void{
		auto res = req.Response( move(j), sl );
		if( contentType.size() )
			res.set( http::field::content_type, contentType );
		stream->AsyncWrite( move(res) );
	}

	Ω send( RestException&& e, sp<IRestStream> stream, sv contentType={} )ι->void{
		auto res = e.Response();
		if( contentType.size() )
			res.set( http::field::content_type, contentType );
		stream->AsyncWrite( move(res) );
	}

	Ω graphQL( HttpRequest req, sp<IRestStream> stream, IRequestHandler* reqHandler )->QL::QLAwait<>::Task{
		constexpr sv contentType = "application/graphql-response+json";
		try{
			let returnRaw = req.Params().contains( "raw" );
			string query;
			jobject vars;
			if( req.IsGet() ){
				query = req["query"];
				auto& varContent = req["variables"];
				vars = varContent.size() ? Json::Parse( move(varContent) ) : jobject{};
			}
			else{
				auto body = req.Body();
				if( auto jquery = body.if_contains("query"); jquery && jquery->is_string() )
					query = jquery->get_string();
				if( auto jvars = body.if_contains("variables"); jvars && jvars->is_object() )
					vars = move( jvars->get_object() );
			}
			THROW_IFX( query.empty(), RestException(EHttpStatus::BadRequest, SRCE_CUR, move(req), "No query sent.") );
			req.LogRead( query, ELogLevel::Trace );
			optional<QL::RequestQL> q;
			try{
				q = QL::Parse( move(query), move(vars), reqHandler->Schemas(), returnRaw );
			}
			catch( runtime_error& e ){
				DBGT( ELogTags::HttpServerRead, "parsing failed: {}", e.what() );
				co_return send( RestException{EHttpStatus::BadRequest, move(e), move(req), "Query parsing failed."}, move(stream), contentType );
			}
			if( Logging::ShouldLog(ELogLevel::Debug, ELogTags::HttpServerRead) ){
				req.LogRead( q->ToString(), ELogLevel::Debug );
			}

			auto result = co_await QL::QLAwait{ move(*q), {req.SessionInfo}, reqHandler->QLServer() };
#ifndef NDEBUG
			auto debugString = serialize(result);
			IO::SaveBinary<char>( fs::temp_directory_path()/"response.json", {debugString.data(), debugString.size()} );
#endif
			jobject y{ {"data", move(result)} };
			send( move(req), move(stream), move(y), contentType );
		}
		catch( RestException& e ){
			send( move(e), move(stream), contentType );
			co_return;
		}
		catch( Access::AccessException& e ){
			send( RestException{e.HttpStatus(), move(e), move(req), "[{}]{}", reqHandler->UserName(e.Executer), e.what()}, move(stream), contentType ); //the exception owns its status - 403 for a denial, 401 only for an unknown executer (access-review3 #17).
		}
		catch( Exception& e ){
			if( !empty(e.Tags & ELogTags::Parsing) )
				send( RestException{EHttpStatus::BadRequest, move(e), move(req), "Query parsing failed."}, move(stream), contentType );
			else if( e.HttpStatus()!=EHttpStatus::InternalServerError )
				send( RestException{move(e), move(req)}, move(stream), contentType );
			else//braced-init sequences left-to-right: the status reads before move(e) - do not switch to parens.
				send( RestException{e.HttpStatus(), move(e), move(req), "Query failed."}, move(stream), contentType );
			co_return;
		}
		catch( runtime_error& e ){
			send( RestException{EHttpStatus::InternalServerError, SRCE_CUR, move(req), "Query failed: {}", e.what()}, move(stream), contentType );
			co_return;
		}
	}

	Ω handleCustomRequest( HttpRequest req, sp<IRestStream> stream, IRequestHandler* reqHandler )ι->IHttpRequestAwait::Task{
		//keep the await in the coroutine frame instead of a temporary: it took ownership of req, and the catch blocks below need
		//the request back to build the error response - a moved-from one loses the session-id authorization header & version.
		auto requestAwait = reqHandler->HandleRequest( move(req) );
		try{
			HttpTaskResult result = co_await *requestAwait;
			THROW_IF( !result.Request, "Request not set." );
			if( result.Body ){//a file of the site (StaticSite), sent as is - the resolver's headers ride on the request's ResponseHeaders.
				DBGT( ELogTags::HttpServerWrite, "HttpResponse:  {} - {}, {} bytes", result.Request->Target(), result.ContentType, result.Body->size() );
				auto res = result.Request->Response<http::string_body>();
				res.set( http::field::content_type, result.ContentType );
				res.body() = move( *result.Body );
				res.prepare_payload();
				stream->AsyncWrite( move(res) );
			}
			else
				send( move(*result.Request), move(stream), move(result.Json), {}, result.Source.value_or(SRCE_CUR) );
		}
		catch( RestException& e ){
			send( move(e), move(stream) );
		}
		catch( Exception& e ){
			if( e.HttpStatus()>=500 )//a relayed 401/409 is the caller's fault - only server faults escalate.
				e.SetLevel( ELogLevel::Critical );
			send( RestException{e.HttpStatus(), move(e), move(requestAwait->Request()), "Error handling request."}, move(stream) );
		}
	}

	//The listener's and ThrowIfPortTaken's, so the check refuses exactly the ports the listener would.
	template<class TAcceptor> Ω exclusiveUse( TAcceptor& acceptor, beast::error_code& ec )ι->void{
#ifdef _WIN32
		//On Windows SO_REUSEADDR lets a second socket bind a port another socket is *listening* on and take its connections, so a Start
		//on a held port succeeded where POSIX fails with EADDRINUSE (SocketTests.StartThrowsWhenPortIsHeld).  SO_EXCLUSIVEADDRUSE is the
		//Windows spelling of "mine alone"; it still allows re-binding over the previous listener's TIME_WAIT connections (Server 2003+).
		acceptor.set_option( net::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>(true), ec );
#else
		acceptor.set_option( net::socket_base::reuse_address(true), ec );//restart over TIME_WAIT; POSIX still refuses a port with a live listener.
#endif
	}
	Ω initListener( typename tcp::acceptor::rebind_executor<executor_with_default>::other& acceptor, const tcp::endpoint& endpoint )ι->bool{
		beast::error_code ec;
		acceptor.open( endpoint.protocol(), ec );
		if( ec ){
			DBGT( ELogTags::App, "!initListener {}:{}", endpoint.address().to_string(), endpoint.port() );
			CodeException{ ec, ELogTags::Server | ELogTags::Http, ELogLevel::Critical };
			return false;
		}

		exclusiveUse( acceptor, ec );
		if( ec ){
			CodeException{ ec, ELogTags::Server | ELogTags::Http, ELogLevel::Critical };
			return false;
		}

		acceptor.bind( endpoint, ec );// Bind to the server address
		if( ec ){
			DBGT( ELogTags::App, "!initListener {}:{}", endpoint.address().to_string(), endpoint.port() );
			CodeException{ ec, ELogTags::Server | ELogTags::Http, ELogLevel::Critical };
			return false;
		}
		acceptor.listen( net::socket_base::max_listen_connections, ec );
		if( ec ){
			CodeException{ ec, ELogTags::Server | ELogTags::Http, ELogLevel::Critical };
			return false;
		}
		return true;
	}

	Ω loadServerCertificate( ssl::context& ctx, const Crypto::CryptoSettings& settings, SRCE )ε->void{
		Crypto::EnsureKeyCertificate( settings, sl );
		ctx.set_options( ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::single_dh_use );
		let cert = IO::Load( settings.Certificate.Path );
		ctx.use_certificate_chain( net::buffer(cert.data(), cert.size()) );

		ctx.set_password_callback( [=](uint, ssl::context_base::password_purpose){return settings.PrivateKey.Passcode;} );
		let key = IO::Load( settings.PrivateKey.Path );
		ctx.use_private_key( net::buffer(key.data(), key.size()), ssl::context::file_format::pem );
		static const string dhStatic =
			"-----BEGIN DH PARAMETERS-----\n"
			"MIIBCAKCAQEArzQc5mpm0Fs8yahDeySj31JZlwEphUdZ9StM2D8+Fo7TMduGtSi+\n"
			"/HRWVwHcTFAgrxVdm+dl474mOUqqaz4MpzIb6+6OVfWHbQJmXPepZKyu4LgUPvY/\n"
			"4q3/iDMjIS0fLOu/bLuObwU5ccZmDgfhmz1GanRlTQOiYRty3FiOATWZBRh6uv4u\n"
			"tff4A9Bm3V9tLx9S6djq31w31Gl7OQhryodW28kc16t9TvO1BzcV3HjRPwpe701X\n"
			"oEEZdnZWANkkpR/m/pfgdmGPU66S2sXMHgsliViQWpDCYeehrvFRHEdR9NV+XJfC\n"
			"QMUk26jPTIVTLfXmmwU0u8vUkpR7LQKkwwIBAg==\n"
			"-----END DH PARAMETERS-----\n";
		string dh = fs::exists( settings.DhPath ) ? IO::Load( settings.DhPath ) : dhStatic;
		ctx.use_tmp_dh( net::buffer(dh.data(), dh.size()) );
	}

	constexpr auto _acceptBackoff{ std::chrono::milliseconds{100} };// short enough that recovery is prompt, long enough that a wedged accept loop is not a busy wait.
	Ω listen( tcp::endpoint endpoint, sp<IRequestHandler> handler )ι->net::awaitable<void, executor_type>{
		typename tcp::acceptor::rebind_executor<executor_with_default>::other acceptor{ co_await net::this_coro::executor };
		if( !initListener(acceptor, endpoint) ){
			DBGT( ELogTags::App, "!initListener" );
			handler->FailStart( Ƒ("could not listen on {}:{}", endpoint.address().to_string(), endpoint.port()) );//this co_return is before handler->Start(), so without it Internal::Start parks in BlockTillStarted forever.
			co_return;
		}

		TRACET( ELogTags::App, "Web Server accepting." );
		handler->Start();
		while( (co_await net::this_coro::cancellation_state).cancelled() == net::cancellation_type::none ){
			auto [ec, sock] = co_await acceptor.async_accept();
			if( ec ){//  Descriptor exhaustion is not a per-connection error - every following
				//accept fails the same way, so the loop spun at 100% cpu.  Pause long enough for closing sessions on the io
				//threads to hand descriptors back, and log it: the old `if(!ec)` left the spin with nothing in the log at all.
				let exhausted = ec==net::error::no_descriptors || ec==boost::system::errc::too_many_files_open_in_system || ec==net::error::no_buffer_space || ec==net::error::no_memory;
				CodeException{ ec, ELogTags::Server | ELogTags::Http, exhausted ? ELogLevel::Error : ELogLevel::Debug };
				if( exhausted ){
					typename net::steady_timer::rebind_executor<executor_with_default>::other backoff{ co_await net::this_coro::executor };
					backoff.expires_after( _acceptBackoff );
					co_await backoff.async_wait();
				}
			}
			else{
				let exec = sock.get_executor();
				beast::error_code endpointEc;
				let userEndpoint = sock.remote_endpoint( endpointEc );
				if( endpointEc ){
					CodeException{ endpointEc, ELogTags::Server | ELogTags::Http, ELogLevel::Debug };
					continue;
				}
				auto cancelSignal = ms<net::cancellation_signal>();
				Execution::AddCancelSignal( cancelSignal );
				net::co_spawn(
					exec,
					detectSession( StreamType(move(sock)), move(userEndpoint), cancelSignal, handler.get() ),
					net::bind_cancellation_slot( cancelSignal->slot(),
					[cancelSignal]( std::exception_ptr e ){
						Execution::RemoveCancelSignal( cancelSignal );
						//Do NOT rethrow: net::detached's handler has an empty body (asio/impl/detached.hpp), so it discards this.
						//Rethrowing lets the exception escape io_context::run(), which kills the executor thread and wedges
						//Process::Shutdown joining it - Opc.Tests hung after its last test with exactly that.  Log instead:
						//it keeps what detached silently dropped visible without changing who unwinds.
						if( e ){
							try{ std::rethrow_exception( e ); }
							catch( const std::exception& x ){ DBGT( ELogTags::Server|ELogTags::Http, "Session ended with an exception: {}", x.what() ); }
						}
					})
				);// We dont't need a strand, since the awaitable is an implicit strand.
			}
		}
	}

	//A second copy of a running product found its port taken only here, in Start - after its whole startup had run against the
	//first's database:  the hub's ended every live app connection and registered itself (reviews/install-issues.md #56).  The same
	//open, option and bind as the listener, on a throwaway acceptor, released before Start binds for real.
	α ThrowIfPortTaken( str addressString, PortType port, SL sl )ε->void{
		net::io_context ioc;
		tcp::acceptor acceptor{ ioc };
		beast::error_code ec;
		let address = net::ip::make_address( addressString, ec );
		if( !ec ){
			let endpoint = tcp::endpoint{ address, port };
			acceptor.open( endpoint.protocol(), ec );
			if( !ec )
				exclusiveUse( acceptor, ec );
			if( !ec )
				acceptor.bind( endpoint, ec );
		}
		if( ec )
			throw Exception{ sl, ELogLevel::Error, "Cannot listen on {}:{} - {}.  Is another copy running?", addressString, port, ec.message() };
	}

	α Internal::Start( sp<IRequestHandler> handler )ε->void{
		loadServerCertificate( handler->Context(), handler->Settings().Crypto() );
		//the server is its own root: anchor the (possibly just-generated) cert for THIS process's web clients, so every
		//embedder gets trust plus register-before-first-client-context ordering by construction - each embedding test
		//main used to repeat the AddTrustAnchor incantation, and a forgotten one surfaced as an opaque TLS failure.
		//Benign in production: the anchor names a cert whose private key this process already holds.
		Client::Ssl::AddTrustAnchor( handler->Settings().Crypto().Certificate.Path );

		let port = handler->Settings().Port();
		let addressString = handler->Settings().Address();
		let address = tcp::endpoint{ net::ip::make_address(addressString), port };
		net::co_spawn(
			*Executor(),
			listen( address, handler ),
			net::bind_cancellation_slot( handler->CancelSignal()->slot(), net::detached )
		);
		Execution::AddCancelSignal( handler->CancelSignal() );
		Execution::Run();
		handler->BlockTillStarted(); // wait for boost to end.
		INFOT( ELogTags::App, "Web Server started:  {}:{}.", address.address().to_string(), address.port() );
	}

	//One map for every listener in the process (OpcHub runs two, Jde.Opc.Tests three): the id comes from NextConnectionIndex,
	//not a per-handler counter, or the second listener's sockets collided with the first's and emplace silently dropped them -
	//Sessions::Remove and Stop then never reached them.  The handler is kept so Stop closes only its own listener's sockets.
	struct SocketEntry{ const IRequestHandler* Handler; sp<IWebsocketSession> Session; };
	concurrent_flat_map<SocketId, SocketEntry> _socketSessions;
	α Internal::NextConnectionIndex()ι->uint32{
		static atomic<uint32> _connectionIndex{};
		return _connectionIndex.fetch_add( 1, std::memory_order_relaxed );
	}
	α Internal::Stop( sp<IRequestHandler>&& handler, bool terminate, SL sl )ι->void{
		handler->Stop( terminate, sl );
		//Close each session, don't just drop the refs: a pending async_read holds the session (and io_context work count)
		//alive, so a peer that never disconnects (e.g. an abandoned client socket) would keep ioc->run() from returning and
		//wedge shutdown at the executor join. Close hops to the stream's strand, so calling it from this (shutdown) thread
		//while io threads run the sessions' handlers is safe; async_close completes the read; the close closure keeps the
		//session alive until then, and OnClose erases it from _socketSessions.
		let mine = [h=handler.get()]( auto& idEntry ){ return idEntry.second.Handler==h; };
		_socketSessions.visit_all( [&mine]( auto& idEntry ){ if( mine(idEntry) ) idEntry.second.Session->Close(); } );
		_socketSessions.erase_if( mine );//server sessions hold beast streams tied to the io_context; drop them here (still in the shutdown-function phase, io_context alive) so they don't outlive it and UAF at static destruction.
	}

	α Internal::RunSocketSession( sp<IWebsocketSession>&& session, const IRequestHandler* handler )ι->void{
		if( !session ){
			WARNT( ELogTags::Socket | ELogTags::Server, "Request handler declined the websocket upgrade;  connection closed." );
			return;
		}
		let id = session->Id();
		_socketSessions.emplace( id, SocketEntry{handler, session} );
		session->Run();
	}

	α Internal::RemoveSocketSession( SocketId id )ι->void{
		TRACET( ELogTags::SocketServerRead, "erased socket: {:x}", _socketSessions.erase(id) );
	}

	//#5: revoking a session has to reach the sockets running under it - Sessions::Remove only drops the _sessions entry, and each
	//IWebsocketSession holds its own sp<SessionInfo>.
	α Internal::CloseSocketSessions( SessionPK sessionId )ι->uint{
		vector<sp<IWebsocketSession>> sessions;
		_socketSessions.cvisit_all( [sessionId, &sessions]( auto& idEntry ){
			if( idEntry.second.Session->SessionId()==sessionId )
				sessions.push_back( idEntry.second.Session );
		} );
		for( auto& session : sessions )//outside the visit: Close net::dispatch'es, so it runs inline when the caller is already on the stream's strand, and its OnClose erases from _socketSessions - which would deadlock under the visit.
			session->Close();
		return sessions.size();
	}
}
	α Server::HandleRequest( HttpRequest req, sp<IRestStream> stream, IRequestHandler* reqHandler )ι->TAwait<sp<SessionInfo>>::Task{
		try{
			req.SessionInfo = co_await Sessions::UpsertAwait( req.Header("authorization"), req.UserEndpoint.address().to_string(), false, reqHandler->AppServer() );
		}
		catch( runtime_error& e ){
			send( RestException{ EHttpStatus::Unauthorized, move(e), move(req), "Could not get sessionInfo."}, move(stream) );
			co_return;
		}
		if( req.IsGet("/graphql") || req.IsPost("/graphql") )
			graphQL( move(req), stream, reqHandler );
		else
			handleCustomRequest( move(req), move(stream), reqHandler );
	}

	//compare codes, not ec.value(): values are only unique within a category, and beast's end_of_stream, asio's stream_truncated
	//and errno's EPERM are all 1.  switching on the value quieted normal keep-alive closes only by that collision, and equally
	//dropped a genuine category-1 error to Trace.
	α Server::ReadSeverity( beast::error_code ec )ι->ELogLevel{
		if( ec==net::error::operation_aborted )
			return ELogLevel::Debug;
		if( ec==http::error::end_of_stream )//peer closed a keep-alive connection - how a session normally ends.
			return ELogLevel::Trace;
		if( ec==ssl::error::stream_truncated )//an SSL "short read": peer closed without performing the required closing handshake.
			return ELogLevel::Trace;
		//ERR_SSL_SSLV3_ALERT_CERTIFICATE_UNKNOWN: the client doesn't trust the certificate.  openssl packs its own codes and asio
		//has no enumerator to name them, so this one is matched on category+value.
		if( ec.category()==net::error::get_ssl_category() && ec.value()==0xA000416 )
			return ELogLevel::Trace;
		return ELogLevel::Error;
	}

	Ω allowMethods()ι->str{
		static const string y = Settings::FindString( "/http/accessControl/allowMethods" ).value_or( "GET, POST, OPTIONS" );
		return y;
	}
	Ω allowHeaders()ι->str{
		static const string y = Settings::FindString( "/http/accessControl/allowHeaders" ).value_or( "Content-Type, Authorization" );
		return y;
	}
	α Server::SendOptions( const HttpRequest&& req )ι->http::message_generator{
		auto res = req.Response<http::empty_body>( http::status::no_content );
		res.set( http::field::access_control_allow_methods, allowMethods() );
		res.set( http::field::accept_encoding, "gzip" );
		res.set( http::field::access_control_allow_headers, allowHeaders() );
		res.set( http::field::access_control_expose_headers, "Authorization" );
		res.set( http::field::access_control_max_age, "7200" ); //2 hours chrome max
		return res;
	}
	α Server::SendServerSettings( HttpRequest req, sp<IRestStream> stream, sp<App::IApp> appClient )ι->Sessions::UpsertAwait::Task{
		jobject j;
		j["restSessionTimeout"] = Chrono::ToString( Sessions::RestSessionTimeout() );
		j["connectionId"] = appClient->ConnectionPK();
		try{
			let session = co_await Sessions::UpsertAwait( req.Header("authorization"), req.UserEndpoint.address().to_string(), false, appClient, false );
			j["active"] = ( bool )session && session->UserPK;
		}
		catch( Exception& e ){
			j["active"] = false;
			e.SetLevel( ELogLevel::Trace );
		}

		send( move(req), move(stream), j, "application/json" );
	}
}
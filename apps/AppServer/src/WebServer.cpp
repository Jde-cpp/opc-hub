#include "WebServer.h"
#include <jde/ql/types/FilterQL.h>
#include <jde/app/IApp.h>
#include <jde/app/log/ProtoLog.h>
#include <jde/access/Authorize.h>//RemoveAdminAuthorizer needs the full type, not just Authorizer()'s forward declaration.
#include "LocalClient.h"
#include "ServerSocketSession.h"
#include "jde/fwk/usings.h"
#include "ql/AppServerQL.h"
#include "appStartup.h"
#include "HttpRequestAwait.h"

#define let const auto
namespace Jde::App{
	using QL::Filter;
	concurrent_flat_map<Server::SocketId,sp<Server::ServerSocketSession>> _sessions;
	sp<Server::RequestHandler> _requestHandler;
	//_sessions is keyed by socket id:  a lookup by anything else is a scan.  Callers act on the result outside the map's bucket lock.
	Ω findSession( auto&& pred )ι->sp<Server::ServerSocketSession>{
		sp<Server::ServerSocketSession> y;
		_sessions.cvisit_while( [&](let& kv){
			if( pred(kv.second) )
				y = kv.second;
			return !y;
		});
		return y;
	}
	Ω sessions( auto&& pred )ι->vector<sp<Server::ServerSocketSession>>{
		vector<sp<Server::ServerSocketSession>> y;
		_sessions.cvisit_all( [&](let& kv){
			if( pred(kv.second) )
				y.push_back( kv.second );
		});
		return y;
	}
	α Server::Schemas()ι->const vector<sp<DB::AppSchema>>&{ return Server::QL().Schemas(); }

	α Server::SetAppPKs( const ConnectionPks& pks )ι->void{
		AppClient()->SetAppPKs( pks.Instance, pks.Connection );
		//ProtoLog needs them for the same reason SubscribeLog is handed them at construction - to tell an entry a client
		//forwarded to us from one of our own coming back.  It cannot take them at construction: Init() runs before the
		//AddConnection round-trip that mints them.
		if( auto log = Logging::FindLogger<App::ProtoLog>(); log )
			log->SetAppPKs( pks.Program, pks.Instance );
	}
	α Server::StartWebServer( jobject&& settings )ε->void{
		StartWebServer( ms<RequestHandler>(move(settings)) );
	}
	α Server::StartWebServer( sp<RequestHandler> handler )ε->void{
		_requestHandler = move( handler );
		Web::Server::Start( _requestHandler );
		Process::AddShutdownFunction( [](bool terminate, SL sl){Server::StopWebServer(terminate, sl);} );//TODO move to Web::Server
	}
	α Server::AddSocketSession( sp<ServerSocketSession> session )ι->void{
		_sessions.emplace( session->Id(), move(session) );
	}
	α Server::RemoveExisting( str host, PortType port )ι->void{
		if( !port )//web_port 0 (a client with no /http/port, e.g. the test binaries) is not a unique endpoint - matching on it would evict every other portless session on the host.
			return;
		vector<sp<ServerSocketSession>> evicted;
		_sessions.erase_if( [&evicted,&host,port](auto&& kv){
			let existing = kv.second->Instance();
			if( existing.host()!=host || existing.web_port()!=port )
				return false;
			evicted.push_back( kv.second );
			return true;
		});
		//Close outside erase_if: Close dispatches to the session's strand and its OnSessionDisconnect re-erases (a no-op now), so it must not run under the map's bucket lock.  Erasing without this left the superseded socket open.
		for( auto& session : evicted )
			session->Close();
	}

	static mutex _claimsMutex;
	static flat_map<Server::SocketId,Proto::FromClient::Instance> _claims;//sockets between their ClaimInstance and publishing the registration
	Server::InstanceClaim::~InstanceClaim(){
		if( !Socket )
			return;
		lg _{ _claimsMutex };
		_claims.erase( Socket );
	}
	α Server::ClaimInstance( Server::SocketId socketId, const Proto::FromClient::Instance& instance )ε->InstanceClaim{
		auto conflicts = [&]( const Proto::FromClient::Instance& other ){
			let sameProcess = other.host()==instance.host() && other.pid()==instance.pid();//one process hosting two roles (Jde.Opc.Tests) shares its identity knowingly.
			let restart = instance.web_port() && other.host()==instance.host() && other.web_port()==instance.web_port();
			return other.application()==instance.application() && other.instance_name()==instance.instance_name() && other.auth_resource()!=instance.auth_resource()
				&& !sameProcess && !restart;
		};
		lg _{ _claimsMutex };
		optional<Proto::FromClient::Instance> holder;
		for( let& [id, claimed] : _claims ){
			if( id!=socketId && conflicts(claimed) ){
				holder = claimed;
				break;
			}
		}
		if( !holder ){
			_sessions.cvisit_while( [&](let& kv){
				if( kv.first!=socketId && kv.second->IsOpen() ){
					if( auto other = kv.second->Instance(); conflicts(other) )
						holder = move( other );
				}
				return !holder;
			});
		}
		THROW_IF( holder, "{} instance '{}' is already registered from {}:{} with resource schema '{}', not '{}' - give each instance on this hub its own /instanceName.", instance.application(), instance.instance_name(), holder->host(), holder->web_port(), holder->auth_resource(), instance.auth_resource() );
		_claims.emplace( socketId, instance );
		return InstanceClaim{ socketId };
	}
	α Server::OnSessionDisconnect( sp<ServerSocketSession> session )ι->void{
		_sessions.erase( session->Id() );
		Authorizer()->RemoveAdminAuthorizer( session );//else a restarted app's stale registration keeps answering admin checks on a dead stream.
		let connectionPK = session->ConnectionPK();
		ForwardExecutionAwait::OnCloseConnection( session->Id(), connectionPK );
		EndConnection( connectionPK );
	}

	α Server::MintJwt( UserPK userPK, string name, string slug, string endpoint, SessionPK sessionId, TimePoint expires, string description )ε->Web::Jwt{
		auto requestHandler = _requestHandler;
		THROW_IF( !requestHandler, "No request Handler." );
		return Web::Jwt{ AppClient()->PublicKey(), userPK, move(name), move(slug), sessionId, move(endpoint), expires, move(description), requestHandler->Settings().Crypto().PrivateKey };
	}

	α Server::StopWebServer( bool terminate, SL sl )ι->void{
		Web::Server::Stop( move(_requestHandler), terminate, sl );
	}

	static mutex _localInstancesMutex;
	static vector<Proto::FromClient::Instance> _localInstances;
	α Server::AddLocalInstance( Proto::FromClient::Instance instance )ι->void{
		lg _{ _localInstancesMutex };
		_localInstances.push_back( move(instance) );
	}
	α Server::FindApplications( str name )ι->vector<Proto::FromClient::Instance>{
		vector<Proto::FromClient::Instance> y;
		{
			lg _{ _localInstancesMutex };
			for( let& instance : _localInstances ){
				if( instance.application()==name )
					y.push_back( instance );
			}
		}
		//A registration is only worth serving while the socket that made it is still up: /opcGateways hands its answer
		//straight to the browser, which has no way to tell a live endpoint from one whose process is tearing down.
		//OnSessionDisconnect normally erases first, but it runs on the session's strand - this closes the window.
		for( let& session : sessions([](let& s){ return s->IsOpen(); }) ){
			if( auto instance = session->Instance(); instance.application()==name )
				y.push_back( move(instance) );
		}
		return y;
	}

	α Server::FindConnection( ConnectionPK connectionPK )ι->sp<ServerSocketSession>{
		if( !connectionPK )//0=session not yet registered.
			return {};
		return findSession( [=](let& s){ return s->ConnectionPK()==connectionPK; } );
	}

	α Server::FindInstance( ProgInstPK instancePK )ι->sp<ServerSocketSession>{
		if( !instancePK )//as FindConnection above:  0 is what every not-yet-registered session reports.  Without this, instance 0 matched whichever socket was still connecting and pushRuntime handed it another instance's log levels (appserver-review3 #18).
			return {};
		return findSession( [=](let& s){ return s->InstancePK()==instancePK; } );
	}

	α Server::QuerySessions( QL::TableQL ql, UserPK executer, SL sl )ι->QuerySessionsAwait{
		auto registered = sessions( [](let& s){ return s->ConnectionPK()!=0; } );//skip not-yet-registered sessions: no status to report and they share ConnectionPK 0.
		return QuerySessionsAwait{ move(ql), executer, move(registered), sl };
	}
	//instancePK names an app instance (App.FromClient.proto app_instance_pk); nullopt = any instance of appPK.  The session, not
	//a write to it:  a caller that must register state keyed to the target learns the connection *before* the request goes on
	//the wire - the reply can come back on another io thread the moment it does (finding #11).
	α Server::FindApp( ProgramPK appPK, optional<ProgInstPK> instancePK )ε->sp<ServerSocketSession>{
		auto y = findSession( [=](let& s){
			let pks = s->Pks();//all three from one publish - matching on Program and then binding the forward to a separately-read Connection is #14's torn window.
			return appPK && pks.Program==appPK && pks.Connection && (!instancePK || pks.Instance==*instancePK);//appPK guard: an unregistered session's ProgramPK is 0, so appPK:0 must not match it.  Connection guard: a forward is keyed by it and 0 is what every unregistered session shares, so a reply could never resolve one bound to it.
		});
		THROW_IF( !y, "No session found for appPK:{}, instancePK:{}", appPK, instancePK ? *instancePK : 0 );
		return y;
	}
}
namespace Jde::App::Server{
	RequestHandler::RequestHandler( jobject&& settings )ι:
		IRequestHandler{ move(settings), Server::AppClient() }
	{}
	α RequestHandler::HandleRequest( HttpRequest&& req, SL sl )ι->up<IHttpRequestAwait>{
		return mu<HttpRequestAwait>( move(req), sl );
	}
	α RequestHandler::WebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<IWebsocketSession>{
		auto session = ms<ServerSocketSession>( move(stream), move(buffer), move(req), move(userEndpoint), connectionIndex );
		AddSocketSession( session );
		return session;
	}
	α RequestHandler::QLServer()ι->sp<QL::IQL>{
		return QLPtr();
	}
}
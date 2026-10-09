#include <jde/fwk/co/Await.h>
#include <jde/fwk/process/process.h>
#include <jde/app/client/IAppClient.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/ql/IQL.h>
#include <jde/access/AccessListener.h>
#include <jde/access/Authorize.h>
#include <jde/access/client/accessClient.h>
#include <jde/app/log/ProtoLog.h>
#include <jde/app/client/appClient.h>
#include <jde/app/client/RemoteLog.h>
#include <jde/app/client/awaits/TaskAdapter.h>
#include <jde/app/client/clientSubscriptions.h>

#define let const auto
namespace Jde::App::Client{
	IAppClient::IAppClient()ι{
		Process::AddShutdown( this );
	}
	IAppClient::~IAppClient(){
		Process::RemoveShutdown( this );//or Process keeps a dangling IShutdown* and calls Shutdown() on freed memory.
	}
	α IAppClient::Shutdown( bool terminate, SL sl )ι->void{
		CloseSocketSession( terminate, sl );
	}
	α IAppClient::InitLogging( sp<App::Client::IAppClient> client )ι->void{
		App::ProtoLog::Init();
		App::Client::RemoteLog::Init( move(client) );
		Logging::Init();
	}

	α IAppClient::Acl( string libName )ι->sp<Access::Authorize>{
		if( !_acl )
			_acl = ms<Access::Authorize>( move(libName) );
		return _acl;
	}
	α IAppClient::Listener()ε->sp<Access::AccessListener>{
		if( !_listener )
			_listener = ms<Access::AccessListener>( QLServer() );
		return _listener;
	}
	α IAppClient::ConfigureAccess( sp<DB::AppSchema> accessSchema, vector<sp<DB::AppSchema>> localSchemas, Jde::UserPK executer, string resourceSchema, SL sl )ε->Access::ConfigureAwait{
		THROW_IFSL( !_acl, "ConfigureAccess before Acl(libName)/SetAcl." );
		_accessContext = Access::Client::Context{ move(localSchemas), _acl, executer, Listener(), move(resourceSchema) };
		//A local client shares the server's Authorize (OpcHub installs it with SetAcl): only its schemas' resource rows need
		//creating - the server's own listener then sees them - and the client loader must not replace the server's snapshot.
		return Access::Client::Configure( move(accessSchema), *_accessContext, QLServer(), IsLocal(), sl );
	}
	α IAppClient::ReloadAccess( SL sl )ε->Access::ConfigureAwait{
		THROW_IFSL( !_accessContext, "ReloadAccess before ConfigureAccess." );
		return Access::Client::Reload( *_accessContext, QLServer(), sl );
	}

	α IAppClient::QueryArray( string&& q, jobject variables, bool returnRaw, SL sl )ε->up<TAwait<jarray>>{
		return QLServer()->QueryArray( move(q), move(variables), UserPK(), returnRaw, sl );
	}
	α IAppClient::QueryObject( string&& q, jobject variables, bool returnRaw, SL sl )ε->up<TAwait<jobject>>{
		return QLServer()->QueryObject( move(q), move(variables), UserPK(), returnRaw, sl );
	}
	α IAppClient::QueryValue( string&& q, jobject variables, bool returnRaw, SL sl )ε->up<TAwait<jvalue>>{
		return QLServer()->Query( move(q), move(variables), UserPK(), returnRaw, sl );
	}
	//The session's request adapted to TAwait, or - with no live session, or shutting down - one that fails without suspending.
	Ω sessionRequest( sp<AppClientSocketSession> session, auto&& request, SL sl )ι->up<TAwait<Web::FromServer::SessionInfo>>{
		using Info = Web::FromServer::SessionInfo;
		if( !session || Process::ShuttingDown() )
			return mu<ExceptionAwait<Info>>( mu<Exception>(Exception{sl, ELogLevel::Debug, "No connection to the AppServer."}), sl );//built in place: through mu the literal would not be a format string.
		return mu<TaskAdapter<Info>>( request(*session), sl );
	}
	α IAppClient::SessionInfoAwait( SessionPK sessionPK, SL sl )ι->up<TAwait<Web::FromServer::SessionInfo>>{
		return sessionRequest( LoadSession(), [&](AppClientSocketSession& s){ return s.SessionInfo(sessionPK, sl); }, sl );
	}

	α IAppClient::AddSession( str domain, str loginName, Access::ProviderPK providerPK, str userEndPoint, bool isSocket, SL sl )ε->up<TAwait<Web::FromServer::SessionInfo>>{
		return sessionRequest( LoadSession(), [&](AppClientSocketSession& s){ return s.AddSession(domain, loginName, providerPK, userEndPoint, isSocket, sl); }, sl );
	}
	α IAppClient::Jwt( SL sl )ε->await<Web::Jwt>{ return Session()->Jwt( sl ); }
	α IAppClient::Login( Web::Jwt&& jwt, SL sl )ε->up<TAwait<Web::FromServer::SessionInfo>>{
		return sessionRequest( LoadSession(), [&](AppClientSocketSession& s){ return s.Login(move(jwt), sl); }, sl );
	}

	α IAppClient::CloseSocketSession( bool terminate, SL sl )ι->void{
		auto session = LoadSession();
		if( !session )
			return;
		let tags = ELogTags::Client | ELogTags::Socket;
		LOGSL( ELogLevel::Trace, sl, tags, "ClosingSocketSession" );
		BlockVoidAwait( session->Close(terminate, sl) );
		session = nullptr;

		LOGSL( ELogLevel::Information, sl, tags, "ClosedSocketSession" );
	}
	α IAppClient::Subscribe( string&& query, jobject variables, sp<QL::IListener> listener, SL sl )ε->await<jarray>{
		return Session()->Subscribe( move(query), move(variables), listener, sl );
	}
	α IAppClient::Unsubscribe( sp<QL::IListener> listener, vector<QL::SubscriptionId> ids, SL sl )ε->void{
		auto session = LoadSession();//not Session(): the live ids are the socket's now, and with no socket there are none to stop - the same no-op this was when _subs was process-wide and already cleared by the close.
		auto removed = session ? session->StopListenRemote( listener, move(ids) ) : flat_set<QL::SubscriptionId>{};
		Subscriptions::Forget( listener, removed );//or the next reconnect would put back what was just unsubscribed.
		QLServer()->Unsubscribe( listener, vector<QL::SubscriptionId>( removed.begin(), removed.end() ), sl );
	}

	α IAppClient::Write( vector<Logging::Entry>&& entries )ι->bool{
		auto session = LoadSession();
		if( !session )
			return false;//losing the session between the caller's check and here is a race it can lose legitimately
		session->Write( FromClient::LogEntries(move(entries)) );
		return true;
	}
}
#pragma once
#include <jde/app/IApp.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/ql/IQL.h>
#include <jde/web/Jwt.h>
#include <jde/app/client/AppClientSocketSession.h>
#include <jde/fwk/process/process.h>
#include <jde/access/client/accessClient.h>

namespace Jde::Access{ struct AccessListener; struct Authorize; }
namespace Jde::App::Client{
	struct AppClientSocketSession;
	struct IAppClient : IApp, IShutdown{
		IAppClient()ι;
		~IAppClient();
		α Shutdown( bool terminate, SL sl )ι->void override;

		Τ using await = Web::Client::ClientSocketAwait<T>;
		//The client's access state.  These were process-wide statics (App::Client::RemoteAcl/SetAcl, a namespace-scope _listener
		//in IAppClient.cpp, Access::Client's configure context) - opcserver-review3 #16:  a process hosting several app clients
		//(Jde.Opc.Tests: AppServer + OpcServer + gateway) had one acl, one listener and the last caller's context, so the gateway's
		//Configure emptied the resources the OpcServer had mapped its nodes to.  Set during startup, before the socket runs.
		α Acl()Ι->sp<Access::Authorize>{ return _acl; }//null until Acl(libName)/SetAcl - clients that never authorize (emulator, soak) keep none.
		α Acl( string libName )ι->sp<Access::Authorize>;//creates on first use;  the name sticks.
		α SetAcl( sp<Access::Authorize> acl )ι->void{ _acl = move(acl); }//a subclass (OpcAuthorize) - install it before anything takes a reference.
		α Listener()ε->sp<Access::AccessListener>;//lazily created, so not const.
		α ConfigureAccess( sp<DB::AppSchema> accessSchema, vector<sp<DB::AppSchema>> localSchemas, Jde::UserPK executer, string resourceSchema, SRCE )ε->Access::ConfigureAwait;//keeps the context for ReloadAccess.
		α ReloadAccess( SRCE )ε->Access::ConfigureAwait;//the same snapshot on the current session;  throws before ConfigureAccess.
		α IsAccessConfigured()Ι->bool{ return _accessContext.has_value(); }//the reconnect path asks before reloading - not every app client has a snapshot to refresh.
		α InitLogging( sp<App::Client::IAppClient> client )ι->void;
		β Connected()Ι->bool{ return LoadSession()!=nullptr; }
		α UserName()Ι->const jobject&{ return _userName; }
		α SetUserName( jobject&& userName )ι->void{ _userName = move(userName); }
		//Virtual with QLServer/AddSession so an embedded client (OpcHub: the gateway hosted beside the AppServer) can answer
		//in-process - the socket bodies stay the default.
		β UserPK()Ι->Jde::UserPK{ auto p=LoadSession(); return p ? p->UserPK() : Jde::UserPK{0}; }
		β QLServer()Ε->sp<QL::IQL>{ auto p=Session(); return p->QLServer(); }
		α PublicKey()Ι->const Crypto::PublicKey& override{ return ServerPublicKey; }

		α SessionInfoAwait( SessionPK sessionPK, SRCE )ι->up<TAwait<Web::FromServer::SessionInfo>> override;
		β AddSession( str domain, str loginName, Access::ProviderPK providerPK, str userEndPoint, bool isSocket, SRCE )ε->up<TAwait<Web::FromServer::SessionInfo>>;//up<TAwait>, as SessionInfoAwait: the socket and the in-process implementations differ in type.
		α Jwt( SRCE )ε->await<Web::Jwt>;
		α Login( Web::Jwt&& jwt, SRCE )ε->up<TAwait<Web::FromServer::SessionInfo>> override;
		α CloseSocketSession( bool terminate, SL sl )ι->void;
		α SessionId()Ι->SessionPK{ auto p=LoadSession(); return p ? p->SessionId() : SessionPK{}; }
		α Subscribe( string&& query, jobject variables, sp<QL::IListener> listener, SRCE )ε->await<jarray>;
		α Unsubscribe( sp<QL::IListener> listener, vector<QL::SubscriptionId> ids, SRCE )ε->void;

		string ResourceSchema;
		optional<Crypto::CryptoSettings> SslSettings;
		Crypto::PublicKey ServerPublicKey;
		vector<sp<DB::AppSchema>> SubscriptionSchemas;
		β Write( vector<Logging::Entry>&& entries )ι->bool;
	private:
		α QueryArray( string&& q, jobject variables, bool returnRaw, SRCE )ε->up<TAwait<jarray>> override;
		α QueryObject( string&& q, jobject variables, bool returnRaw, SRCE )ε->up<TAwait<jobject>> override;
		α QueryValue( string&& q, jobject variables, bool returnRaw, SRCE )ε->up<TAwait<jvalue>> override;
		α SetSession( sp<AppClientSocketSession> session )ι->void{ lg _{_sessionMutex}; _session = move(session); }
		α LoadSession()Ι->sp<AppClientSocketSession>{ lg _{_sessionMutex}; return _session; }
		α Session()Ε->sp<AppClientSocketSession>{ auto p = LoadSession(); THROW_IF( !p, "Not connected." ); THROW_IF( Process::ShuttingDown(), "Shutting down." ); return p; }

		jobject _userName;
		mutable mutex _sessionMutex;
		sp<AppClientSocketSession> _session;
		sp<Access::Authorize> _acl;
		sp<Access::AccessListener> _listener;
		optional<Access::Client::Context> _accessContext;

		friend struct AppClientSocketSession; friend struct ConnectAwait;
	};
}
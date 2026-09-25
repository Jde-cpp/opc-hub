#include "appStartup.h"
#include <jde/fwk/co/Await.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/io/Cache.h>
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/access/server/accessServer.h>
#include <jde/access/Authorize.h>
#include <jde/access/AccessListener.h>
#include <jde/web/server/IRequestHandler.h>
#include <jde/web/server/Server.h>
#include <jde/web/server/SessionGraphQL.h>
#include <jde/web/server/SettingQL.h>
#include <jde/web/server/SubscribeLog.h>
#include "WebServer.h"
#include "LocalClient.h"
#include "ql/AppInstanceHook.h"
#include "ql/AppServerQL.h"

#define let const auto
namespace Jde::App{
	static sp<DB::AppSchema> _appSchema;
	Ω ds()ι->DB::IDataSource&{ return *_appSchema->DS(); }
	Ω connectionTableName()ε->string{ return _appSchema->GetView("connections").DBName; }
	//ends every connection still open - the ones earlier runs left behind.  ConfigureDS calls it once, before AddConnection.
	Ω endAppInstances()ε->void{
		ds().ExecuteSync( {Ƒ("update {} set deleted={} where deleted is null", connectionTableName(), ds().Syntax().UtcNow())} );
	}

	α AddConnection( str appName, str instanceName, str hostName, uint pid, bool reloadHosts )ε->tuple<ProgramPK, ProgInstPK, ConnectionPK>{
		ProgramPK appId{};
		ProgInstPK appInstanceId{};
		ConnectionPK appConnectionId{};
		let rows = ds().Select( {
			Ƒ("{}(?,?,?,?)", _appSchema->GetTable("connections").InsertProcName()),
			{DB::Value{appName}, {instanceName}, DB::Value{hostName}, DB::Value{pid}},
			true} );
		for( auto&& row : rows ){
			appId = row.Get<uint32_t>(0);
			appInstanceId = row.Get<uint32_t>(1);
			appConnectionId = row.Get<uint32_t>(2);
		}

		//`hosts` has the enum shape (id + name) and is loaded like one, but unlike the real enumerations it grows - the proc
		//above adds a row the first time a host registers, outside QL entirely, and instances.hostId renders through it.
		//Re-loaded rather than cleared: a cleared entry hands the next render the blocking miss this exists to remove.  The
		//Select above is a plain blocking driver call, safe anywhere;  LoadEnum is SelectEnumSync - a BlockAwait over a query
		//co_spawned onto the executor pool - so it is only for callers off that pool (startup, tests).  The request path
		//(ServerSocketSession::AddInstance) passes false and co_awaits ReloadHosts instead:  with executor.threads:2, two
		//registrations in the same instant parked both threads here with their queries queued behind them, an AppServer
		//bounce with two instances alive never completed either, and the OpcServer died on its startup timeout
		//(emulator-review W1 - the threads:2 case db-review3 #1 predicted).
		if( reloadHosts )
			QL::LoadEnum( _appSchema->GetTable("hosts") );
		return make_tuple( appId, appInstanceId, appConnectionId );
	}
	α ReloadHosts( SL sl )ε->DB::CacheAwait<flat_map<uint,string>>{
		let& hosts = _appSchema->GetTable( "hosts" );
		Cache::Clear( hosts.Name );//SelectEnum answers from the cache when it can - clear first so the select actually runs.
		return ds().SelectEnum<uint,string>( hosts, nullopt, sl );//nullopt: no expiry, as LoadEnum caches it.
	}
	α EndConnection( ConnectionPK connectionId, SL sl )ι->DB::ExecuteAwait::Task{
		try{
			co_await ds().Execute( {Ƒ("update {} set deleted={} where connection_id=? and deleted is null", connectionTableName(), ds().Syntax().UtcNow()), {DB::Value{connectionId}}}, sl );
		}
		catch( runtime_error& )
		{}
	}
}

namespace Jde::App::Server{
	static sp<Access::AccessListener> _listener;

	α InitLogging()ι->void{
		AppClient()->InitLogging();
	}
	α AppSchema()ι->sp<DB::AppSchema>{ return _appSchema; }

	Ω configureDS( ConfigureOptions& options )ε->void{
		auto authorizer = Authorizer();
		auto accessSchema = DB::GetAppSchema( "access", authorizer );
		_appSchema = DB::GetAppSchema( "app", authorizer );
		vector<sp<DB::AppSchema>> schemas{ accessSchema, _appSchema };
		schemas.insert( schemas.end(), options.ExtraSchemas.begin(), options.ExtraSchemas.end() );

		if( options.MakeQL ){
			QL::Configure( schemas );
			SetQL( options.MakeQL(schemas, authorizer) );
		}
		else
			ConfigureQL( schemas, authorizer );
		_listener = ms<Access::AccessListener>( QLPtr() );
		Process::AddShutdownFunction( []( bool terminate, SL sl ){
			_listener->Shutdown( terminate, sl );
			_listener = nullptr;
		});

		let recreate = Settings::FindBool( "/testing/recreateDB" ).value_or( false );
		let sync = recreate || Settings::FindBool("/dbServers/sync").value_or(false) || accessSchema->DS()->RequiresSync();//decided once:  RequiresSync answers differently once the tables exist.
		if( recreate ){
			for( let& schema : schemas )
				DB::NonProd::Recreate( *schema, QLPtr() );
		}
		else if( sync ){
			for( let& schema : schemas )
				DB::SyncSchema( *schema, QLPtr() );
		}
		QL::LoadEnums( schemas );
		BlockVoidAwait( Access::Server::Configure(vector<sp<DB::AppSchema>>{schemas}, QLPtr(), UserPK{UserPK::System}, authorizer, _listener) );//the access load is a coroutine chain; this is the sync api over it.
		if( sync ){//the role seeds (<schema>.roles):  createRole/addRole run through the access server's mutations and its acl gate, which Configure just installed - SyncSchema's .mutation pass ran before them, so a role there died with the process (setup/README.md).
			for( let& schema : schemas ){
				try{
					DB::SyncData( *schema, QLPtr(), ".roles", true );//true:  a file unchanged since it applied is skipped, so an admin's edits to a seeded role survive (reviews/m3-closing.md #12)
				}
				catch( runtime_error& e ){//not worth the hub:  it would stay down, and with it the UI that could repair the row - e.g. a seeded role whose slug an admin renamed but not its name, so the probe misses and the create dies on the name index (reviews/m3-closing.md #1).  Here, not in SeedData:  the .mutation pass shares it, and a failed enum seed should stay fatal.
					CRITICALT( ELogTags::App, "[{}]The role seed did not apply, the roles it ships may be missing or stale:  {}", schema->Name, e.what() );
				}
			}
		}
		endAppInstances();
	}

	α Configure( const jobject& webServerSettings, ConfigureOptions options )ε->void{
		//first:  a second copy of a running server found its port taken only at StartWebServer - after configureDS's sync and seeds,
		//endAppInstances and AddConnection had run against the first's database, ending every live app connection (reviews/install-issues.md #56).
		Web::Server::IRequestHandler::WebServerSettings listener{ webServerSettings };
		Web::Server::ThrowIfPortTaken( listener.Address(), listener.Port() );
		configureDS( options );
		str instanceName{ Settings::FindString("/instanceName").value_or(_debug ? "Debug" : "Release") };
		let pks = AddConnection( Process::AppName(), instanceName, Process::HostName(), Process::ProcessId() );
		Logging::Add<Web::Server::SubscribeLog>( "subscribe", get<0>(pks), get<1>(pks) );
		SetAppPKs( pks );

		QL::SetSystemTables( {"adminCheck", "apps", "connections", "logSetting"} );
		auto appClient = AppClient();
		Crypto::CryptoSettings sslSettings{ Json::FindDefaultObject(webServerSettings, "ssl") };
		Crypto::EnsureKeyCertificate( sslSettings );//the listener used to create the key as a side effect, which is why SetPublicKey had to wait for it.
		appClient->SetPublicKey( sslSettings.PublicKey.Value(SRCE_CUR) );
		QL::Hook::Add( mu<AppInstanceHook>(appClient) );
		QL::Hook::Add( mu<Web::Server::SessionGraphQL>(appClient, Authorizer()) );
	}

	α AppStartup( jobject webServerSettings )ε->void{
		Configure( webServerSettings );
		StartWebServer( move(webServerSettings) );
		AppClient()->LoadLogSettings();
		INFOT( ELogTags::App, "--AppServer Started.--" );
	}
}

#include "gatewayStartup.h"
#include <jde/fwk/co/Await.h>
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/ql/ql.h>
#include <jde/ql/LocalQL.h>
#include <jde/ql/types/Introspection.h>
#include <jde/access/Authorize.h>
#include <jde/access/AccessListener.h>
#include <jde/access/client/accessClient.h>
#include <jde/app/client/appClient.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/ServerTrust.h>
#include "jde/fwk/settings.h"
#include "GatewayAppClient.h"
#include "opcInternal.h"
#include "UAClient.h"
#include "WebServer.h"
#include "ql/GatewayQL.h"
#include "ql/GatewayQLAwait.h"
#include "ql/HistGroupInfoQLAwait.h"
#include "ql/OpcSessionsQLAwait.h"
#include "ql/SearchQLAwait.h"
#include "hist/HistHost.h"
#include "ql/HistGroupQLAwait.h"
#include "ql/HistQL.h"
#include "ql/OpcQLHook.h"
#include "ql/TestConnectionQLAwait.h"

#define let const auto
namespace Jde::Opc{
	α Gateway::Configure( const jobject& webServerSettings, jobject userName, sp<QL::LocalQL> ql )ε->sp<DB::AppSchema>{
		if( userName.empty() )
			userName = jobject{ {"name", Ƒ("OpcGateway-{}", Process::HostName())} };
		auto appClient = AppClient();
		auto authorize = appClient->Acl( "gateway" );
		auto schema = DB::GetAppSchema( "gateway", authorize );
		if( ql )
			SetQL( move(ql) );
		else
			ConfigureQL( {schema}, authorize );
		for( let& path : Settings::FindPathArray("/ql/introspection") )
			QL::AddIntrospection( QL::Introspection{Json::ReadJsonNet(Settings::Directory()/path)} );
		//logSetting, the app client's;  sessions, the AppServer's ask for this process's web sessions, answered only over its socket (GatewayAppClient::ClientQuery).
		flat_set<string> tables{ "logSetting", "sessions", string{SearchQLAwait::Command} };
		for( let family : {std::span<const sv>{GatewayQLAwait::Tables}, std::span<const sv>{HistGroupInfoQLAwait::Tables}, std::span<const sv>{ServerCnnctnSessionsQLAwait::Tables}} ){
			for( let table : family )
				tables.emplace( table );
		}
		QL::SetSystemTables( move(tables) );
		flat_set<string> mutations{ string{HistGroupMAwait::ResolveCommand}, string{TestConnectionQLAwait::Command} };//resolveHistory, testConnection:  resolve and test are no verbs either.
		for( let command : HistQL::EditCommands )
			mutations.emplace( command );
		QL::SetSystemMutations( move(mutations) );//createHistory…, taken whole:  upsert is no verb, and their results keep their shape.
		SetSchema( schema );
		if( ql ){}//the host's QL owns the schema list, and synced it with the rest.
		else if( Settings::FindBool("/testing/recreateDB").value_or(false) )
			DB::NonProd::Recreate( *schema, QLPtr() );
		else if( Settings::FindBool("/dbServers/sync").value_or(false) || schema->DS()->RequiresSync() )
			DB::SyncSchema( *schema, QLPtr() );

		//value_or defaults mirror config/Opc.Gateway.jsonnet - keep them in lockstep so a deployment missing a key runs what the shipped config documents.
		_pingInterval = Settings::FindDuration("/gateway/pingInterval").value_or( 30s );
		_ttl = Settings::FindDuration("/gateway/ttl").value_or( 2min );
		ServerTrust::EnsureDirs( "/gateway" );//this product's ssl/servers - where a third-party OPC server's certificate goes - exists from the first start.
		appClient->Enroll( webServerSettings );//the AppServer login's JWT and the OPC certificate authentication both sign with it.
		appClient->SetUserName( move(userName) );
		return schema;
	}
	α Gateway::Connect( sp<DB::AppSchema> schema )ε->void{
		auto appClient = AppClient();
		auto accessSchema = DB::GetAppSchema( "access", appClient->Acl() );
		appClient->SubscriptionSchemas.push_back( accessSchema );
		if( !appClient->IsLocal() )//OpcHub: the AppServer is this process - no login/socket; the hub set the pks and acl before calling.
			BlockVoidAwait( App::Client::ConnectAwait{appClient, false} );
		if( Settings::FindBool("/logging/loadFromServer").value_or(true) )
			appClient->LoadLogSettings();

		BlockVoidAwait( appClient->ConfigureAccess(accessSchema, {move(schema)}, UserPK{UserPK::System}, {}) );
		Process::AddShutdownFunction( [listener=appClient->Listener()](bool terminate, SL sl){ listener->Shutdown(terminate, sl); } ); //as the AppServer does - the subscriptions otherwise outlive everything they reference (access-review3 #25).
		Process::AddShutdownFunction( [](bool, SL){ HistHost::Instance().Stop(); } );//before the UA clients go:  the collectors' items are dropped on live clients, and the groups' last flushes run while the executor still does.
		Process::AddShutdownFunction( [](bool terminate, SL sl){UAClient::Shutdown(terminate, sl);} );
		QL::Hook::Add( mu<OpcQLHook>() );
		HistHost::Instance().Start();//after the access snapshot:  a change from here on carries its writer's name.
	}
	α Gateway::Startup( jobject webServerSettings, jobject userName )ε->void{
		auto schema = Configure( webServerSettings, move(userName) );
		StartWebServer( move(webServerSettings) );//must follow Configure's _pingInterval/_ttl assignments - requests can hit ProcessingLoop once the server is up.
		Connect( move(schema) );
		INFOT( ELogTags::App, "---Started {}---", "OPC Gateway" );
	}
}
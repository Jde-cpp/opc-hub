#include "gatewayStartup.h"
#include <jde/fwk/co/Await.h>
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/ql/ql.h>
#include <jde/ql/LocalQL.h>
#include <jde/ql/types/Introspection.h>
#include <jde/access/Authorize.h> //!important
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
#include "ql/OpcQLHook.h"

#define let const auto
namespace Jde::Opc{
	//Defined in async/AsyncRequest.cpp as Jde::Opc::Gateway::_pingInterval/_ttl - the extern must sit in that same namespace or it
	//declares a distinct Jde::Opc:: symbol that no TU defines (undefined at link).  27fe137b moved startup here and flattened it.
	namespace Gateway{ extern Duration _pingInterval; extern Duration _ttl; }
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
		QL::SetSystemTables( {"connectionStatus", "dataType", "dataTypes", "discoveryUrls", "logSetting", "namespaces", "node", "nodes", "opcConnections", "opcSessions", "search", "securityMode", "securityPolicyUri", "serverDescription", "variable", "variables"} );
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
		Crypto::CryptoSettings sslSettings{ Json::FindDefaultObject(webServerSettings, "ssl") };
		Crypto::EnsureKeyCertificate( sslSettings );
		appClient->SslSettings = move(sslSettings);//the AppServer login's JWT and the OPC certificate authentication both sign with it.
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
		Process::AddShutdownFunction( [](bool terminate, SL sl){UAClient::Shutdown(terminate, sl);} );
		QL::Hook::Add( mu<OpcQLHook>() );
	}
	α Gateway::Startup( jobject webServerSettings, jobject userName )ε->void{
		auto schema = Configure( webServerSettings, move(userName) );
		StartWebServer( move(webServerSettings) );//must follow Configure's _pingInterval/_ttl assignments - requests can hit ProcessingLoop once the server is up.
		Connect( move(schema) );
		INFOT( ELogTags::App, "---Started {}---", "OPC Gateway" );
	}
}
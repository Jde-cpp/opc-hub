#include "gatewayStartup.h"
#include <jde/fwk/co/Await.h>
#include <jde/db/db.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/ql/ql.h>
#include <jde/ql/LocalQL.h>
#include <jde/ql/types/Introspection.h>
#include <jde/access/Authorize.h>
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
#include "ql/ServerCnnctnMAwait.h"
#include "hist/HistHost.h"
#include "ql/HistGroupQLAwait.h"
#include "ql/HistQL.h"
#include "ql/TestConnectionQLAwait.h"
#include "server/GatewayServer.h"

#define let const auto
namespace Jde::Opc{
	static optional<Crypto::CryptoSettings> _processSsl;//the listener's pair, Configure's, which the endpoint presents when its block names none (spec *Startup and deploy*).
	α Gateway::Configure( const jobject& webServerSettings, sp<QL::LocalQL> ql )ε->sp<DB::AppSchema>{
		auto appClient = AppClient();
		auto authorize = appClient->Acl( "gateway" );
		auto schema = DB::GetAppSchema( "gateway", authorize );
		let hosted = ql!=nullptr;//asked after the move below, ql would always read empty.
		if( hosted )
			SetQL( move(ql) );
		else
			ConfigureQL( {schema}, authorize );
		for( let& path : Settings::FindPathArray("/ql/introspection") )
			QL::AddIntrospection( QL::Introspection{Json::ReadJsonNet(Settings::Directory()/path)} );
		//logSetting, the app client's;  sessions, the AppServer's ask for this process's web sessions, answered only over its socket (GatewayAppClient::ClientQuery).
		flat_set<string> tables{ "logSetting", "sessions", string{SearchQLAwait::Command}, string{EndpointQLAwait::Command} };//endpoint:  the gateway's own (OpcSessionsQLAwait.h);  its sub-tables inherit.
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
		if( !hosted )//the host's QL owns the schema list, and synced it with the rest.
			DB::SyncOrRecreate( {schema}, QLPtr() );

		//value_or defaults mirror config/Opc.Gateway.jsonnet - keep them in lockstep so a deployment missing a key runs what the shipped config documents.
		_pingInterval = Settings::FindDuration("/gateway/pingInterval").value_or( 30s );
		_ttl = Settings::FindDuration("/gateway/ttl").value_or( 2min );
		Process::RaiseTimerResolution();//AsyncRequest's 1 ms poll for a UA response took a 15.6 ms tick on windows:  every round trip's floor.
		ServerTrust::EnsureDirs( "/gateway" );//this product's ssl/servers - where a third-party OPC server's certificate goes - exists from the first start.
		appClient->Enroll( webServerSettings );//the AppServer login's JWT and the OPC certificate authentication both sign with it.
		_processSsl.emplace( Json::FindDefaultObject(webServerSettings, "ssl"), sv{} );
		ServerCnnctnMAwait::SetSynced();//the hub synced in its own Configure, before this.
		return schema;
	}
	α Gateway::Connect( sp<DB::AppSchema> schema )ε->void{
		App::Client::ConnectAccess( AppClient(), {move(schema)}, {}, false );
		//Registration order is run order (process.cpp):  the endpoint first, so its sessions are gone before the historian and the UA
		//clients it reports on;  then the historian, before the UA clients go:  the collectors' items are dropped on live clients, and
		//the groups' last flushes run while the executor still does.
		Process::AddShutdownFunction( [](bool, SL){ GatewayServer::Stop(); } );
		Process::AddShutdownFunction( [](bool, SL){ HistHost::Instance().Stop(); } );
		Process::AddShutdownFunction( [](bool terminate, SL sl){UAClient::Shutdown(terminate, sl);} );
		THROW_IF( !_processSsl, "Connect before Configure." );
		GatewayServer::Listen( *_processSsl );//before the groups load:  the endpoint hears each member from its first value.
		HistHost::Instance().Start();//after the access snapshot:  a change from here on carries its writer's name.
		GatewayServer::Start();//last:  the hub and the standalone gateway both reach here, after the groups are loaded.
	}
	α Gateway::Startup( jobject webServerSettings )ε->void{
		auto schema = Configure( webServerSettings );
		StartWebServer( move(webServerSettings) );//must follow Configure's _pingInterval/_ttl assignments - requests can hit ProcessingLoop once the server is up.
		Connect( move(schema) );
		INFOT( ELogTags::App, "---Started {}---", "OPC Gateway" );
	}
}
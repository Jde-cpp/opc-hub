#include "opcServerStartup.h"
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>//RequiresSync;  no longer in the pch - the app schema is the only db surface left.
#include <jde/ql/ql.h>
#include <jde/access/AccessListener.h>
#include <jde/access/client/accessClient.h>
#include <jde/app/client/appClient.h>
#include "UAServer.h"
#include "access/OpcAuthorize.h"
#include "web/WebServer.h"

#define let const auto
namespace Jde::Opc{
	α Server::Startup( jobject webServerSettings, jobject userName )ε->void{
		auto appClient = AppClient();
		sp<OpcAuthorize> opcAuthorize;
		{
			auto schemaSuffix = Settings::FindString( "/opcServer/resource" );
			appClient->SetAcl( opcAuthorize=ms<OpcAuthorize>(schemaSuffix ? "opc." + *move(schemaSuffix) : "opc") );
		}
		auto remoteAcl = appClient->Acl();
		auto uaSchema = DB::GetAppSchema( "opc", remoteAcl );
		uaSchema->Authorizer = opcAuthorize;// GetAppSchema returns a cached schema whose Authorizer is baked in when GetClusters first builds the cache. When another server (e.g. embedded AppServer) built it first with a base Access::Authorize, our SetAcl(OpcAuthorize) above is ignored here. Install it explicitly so UAAccess::GetUserAccessLevel's static_cast<OpcAuthorize&> is valid (and so UserRights reads the same _nodeResources that AssignRights populates).
		ConfigureQL( uaSchema, remoteAcl );
		QL::SetSystemTables( {"logSetting", "adminCheck"} );//adminCheck: the AppServer's delegated admin check (OpcServerQL) - a name no schema owns, so QL::Parse admits it without a view; `permissionRight` would shadow the real table for an embedded AppServer.
		if( Settings::FindBool("/testing/recreateDB").value_or(false) )
			DB::NonProd::Recreate( *uaSchema, QLPtr() );
		else if( Settings::FindBool("/dbServers/sync").value_or(false) || uaSchema->DS()->RequiresSync() )
			DB::SyncSchema( *uaSchema, QLPtr() );
		Crypto::CryptoSettings settings{ Json::FindDefaultObject(webServerSettings,"ssl") };
		Crypto::EnsureKeyCertificate( settings );
		appClient->SslSettings = settings;
		StartWebServer( move(webServerSettings) ); //TODO take out.
		auto accessSchema = DB::GetAppSchema( "access", remoteAcl );
		appClient->SubscriptionSchemas.push_back( accessSchema );

		auto resourceSchema = Settings::FindString( "/opcServer/resource" ).value_or( "" );
		appClient->ResourceSchema = resourceSchema.size() ? Ƒ( "opc.{}", resourceSchema ) : "opc";

		appClient->SetUserName( move(userName) );
		//retry: the hub is this process's registry, and a first start of the hub - schema sync, keys, then its certificate - can
		//outlast the installer's head start or a logon's, so the login here used to fail its verify and end the process
		//(install-issues #12).  Now it waits for the hub, a warning per attempt, for as long as it takes or until shutdown;
		//the client TLS context picks the hub's certificate up when it appears (Web::Client::Ssl::Context).  The block's own
		//stall line is capped at Information: it read like a hang - a warning at 30 s, criticals after - beside the retry
		//warnings that say what the wait is for (the 09-15 rerun's retry table).
		BlockVoidAwait( App::Client::ConnectAwait{appClient, true}, ELogLevel::Information );
		appClient->LoadLogSettings();

		BlockVoidAwait( appClient->ConfigureAccess(accessSchema, {uaSchema}, UserPK{UserPK::System}, resourceSchema) );
		Process::AddShutdownFunction( [listener=appClient->Listener()](bool terminate, SL sl){ listener->Shutdown(terminate, sl); } ); //as the AppServer does - the subscriptions otherwise outlive everything they reference (access-review3 #25).
		Initialize( uaSchema );
		auto& ua = GetUAServer();
		for( let& config : Settings::FindPathArray("/opcServer/configFiles") )
			ua.Load( config );
		//Both before Run(), which is what opens the listener.  AssignRights browses the address space, and the nodestore
		//(ns0 included) is built by UA_Server_newWithConfig, so it needs the nodesets loaded, not the server started.
		//Started first, the browse deadlocked against a client Read on the UA thread (opcserver-review3 #10) and, until it
		//finished, every session that connected in the window got EAccess::All on every node because _enabled was still
		//false (L21).
		opcAuthorize->AssignRights( ua );
		ua.Run();
		if( let pubsub = Settings::FindObject("/opcServer/pubsub"); pubsub )//after the nodesets: the reader's targets are browse paths into them.
			StartPubSub( *pubsub );

		for( let& [idx, ns] : ua.Namespaces() )
			INFOT( ELogTags::App, "ns: {}, uri: {}", idx, ns );

		INFOT( ELogTags::App, "---Started OPC Server---" );
	}
}
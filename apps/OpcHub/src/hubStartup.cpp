#include "hubStartup.h"
#include <jde/fwk/co/Await.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/db/db.h>
#include <jde/access/Authorize.h>
#include <jde/app/client/IAppClient.h>
#include <jde/app/IApp.h>
#include "../../AppServer/src/appStartup.h"
#include "../../AppServer/src/LocalClient.h"
#include "../../AppServer/src/WebServer.h"
#include "../../OpcGateway/src/gatewayStartup.h"
#include "../../OpcGateway/src/WebServer.h"
#include "../../OpcGateway/src/ql/GatewayQL.h"
#include "HubAppClient.h"
#include "WebServer.h"
#include "ql/HubQL.h"

#define let const auto
namespace Jde::Opc::Hub{
	constexpr ELogTags _tags{ ELogTags::App };
	α Startup( jobject httpSettings, jobject gatewayCredentials, function<void()> afterWebServer )ε->void{
		//1. The in-process client replaces the gateway's socket client before anything captures AppClient(), and shares the
		//AppServer's Authorize: Gateway::Configure's Acl("gateway") returns it, so GatewayQL/HubQL, Table::Authorize and the listener's
		//UserName all consult the one snapshot the AppServer maintains.
		auto hub = ms<HubAppClient>();
		Opc::Gateway::SetAppClient( hub );
		auto authorizer = App::Server::Authorizer();
		hub->SetAcl( authorizer );

		//2. The AppServer role over all three schemas: the gateway's joins the sync, the enum load and the access snapshot (its
		//search/sessions resources gate server-side), and the QL is the HubQL - installed into both apps' singletons.
		auto gatewaySchema = DB::GetAppSchema( "gateway", authorizer );//the first GetAppSchema binds every schema to this Authorize - the AppServer's, as its own Configure would.
		App::Server::Configure( httpSettings, {
			.ExtraSchemas = { gatewaySchema },
			.MakeQL = []( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> auth )->sp<QL::LocalQL> {
				auto ql = ms<HubQL>( move(schemas), move(auth) );
				Opc::Gateway::SetQL( ql );
				return ql;
			}
		} );
		//3. The process registered once (AddConnection in Configure); the gateway's log-settings load keys on InstancePK().
		auto appServer = App::Server::AppClient();
		hub->SetAppPKs( appServer->InstancePK(), appServer->ConnectionPK() );

		//4. The gateway role's schema, introspection, ping/ttl and certificate - its QL is the HubQL, its listener the hub's.
		Opc::Gateway::Configure( httpSettings, move(gatewayCredentials), App::Server::QLPtr() );

		//5. The one listener.  Its shutdown closes the app sockets; the gateway's registry needs its own close, as its listener's did.
		auto handler = ms<Hub::RequestHandler>( move(httpSettings) );
		let port = handler->Settings().Port();//the port the listener binds, its default included - what /opcGateways advertises below.
		App::Server::StartWebServer( move(handler) );
		Process::AddShutdownFunction( Opc::Gateway::Server::Shutdown );
		appServer->LoadLogSettings();
		if( afterWebServer )
			afterWebServer();

		//6. The gateway wired to the AppServer in-process: no login/socket, this instance's log settings, its access resources.
		Opc::Gateway::Connect( move(gatewaySchema) );

		//7. What /opcGateways serves the SPA: the gateway role of this process, at the one port.  Built here rather than with
		//FromClient::Instance so the fields come from the listener's settings, not a client's.
		App::Proto::FromClient::Instance instance;
		instance.set_application( "Jde.OpcGateway" );
		instance.set_instance_name( App::InstanceName() );//the name Configure's AddConnection registers, so connections{} and /opcGateways agree.
		instance.set_host( Settings::FindString("/http/host").value_or(Process::HostName()) );//as FromClient::Instance: the host the browser reaches us by.
		instance.set_pid( Process::ProcessId() );
		*instance.mutable_start_time() = Protobuf::ToTimestamp( Process::StartTime() );
		instance.set_web_port( port );
		App::Server::AddLocalInstance( move(instance) );
		INFO( "--OpcHub Started.--" );
	}
}
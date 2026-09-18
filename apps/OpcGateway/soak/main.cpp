#include <jde/fwk.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/app/client/IAppClient.h>
#include <jde/app/client/appClient.h>
#include <jde/opc/uatypes/Logger.h>
#include "../src/UAClient.h"
#include "SoakAppClient.h"
#include "SoakRunner.h"

#define let const auto
#ifndef _MSC_VER
	α Jde::Process::ProductName()ι->sv{ return "Opc.Soak"; }
#endif

namespace Jde::Opc::Gateway::Soak{
	constexpr ELogTags _tags{ ELogTags::Test };
	//Creates the gateway's client certificate for each active soak leg. Recommended before OpcServer starts, no longer
	//required for the Jde OpcServer (UATrust rescans trustedCertDirs on a failed verify) - still required for external
	//servers that snapshot their trust list, and it prints the path they need to anchor. Goes through UAClient's own
	//helpers so the files land exactly where the gateway process will look for them; /gateway/issuedCerts in
	//Opc.Soak.jsonnet supplies the gateway's product and CN.
	Ω createGatewayCerts()ε->void{
		for( let& leg : ActiveServers() ){
			if( leg.CertificateUri.empty() ){//no uri -> the gateway connects with SecurityPolicy None and presents no cert on the channel;  the one it encrypts the user token with (UAClient::Configuration) it issues itself at connect, and nobody has to trust it.
				INFO( "No certificateUri for '{}' - skipping certificate.", leg.Slug );
				continue;
			}
			UAClient::EnsureCertificate( leg.Slug );//its SAN is the gateway's own applicationUri (/gateway/issuedCerts), not the leg's certificateUri - security-matrix #8.
			let certificateFile = UAClient::CryptoSettings( leg.Slug ).Certificate.Path;
			INFO( "Gateway certificate ready: {}.", certificateFile.string() );
			if( leg.User.size() )
				INFO( "External server '{}': trust {} in its server configuration before the run.", leg.Slug, certificateFile.string() );
		}
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	using namespace Jde::Opc::Gateway;
	Logging::AddTagParser( mu<Opc::UALogParser>() );
	int exitCode{ EXIT_FAILURE };
	try{
		Process::Startup( argc, argv, "Jde.Opc.Soak", "OpcGateway soak driver" );
		if( Process::FindArg("-createCert") ){
			Soak::createGatewayCerts();
			exitCode = EXIT_SUCCESS;
		}
		else{
			auto client = Soak::AppClient();
			client->InitLogging( client );
			Crypto::CryptoSettings ssl{ Json::FindDefaultObject(Settings::AsObject("/http"), "ssl"), Process::ProductName() };//the file name doubles as the subject CN - the soak client's enrollment identity.
			Crypto::EnsureKeyCertificate( ssl );
			client->SslSettings = ssl;
			client->SetUserName( jobject{Settings::AsObject("/credentials")} );
			Execution::Run();//without proto/remote logging or a web server, nothing else starts the executor thread the socket/http awaitables need.
			BlockVoidAwait( App::Client::ConnectAwait{client, false} );
			if( Process::FindArg("-grant") ){//soak.sh runs this after OpcServer's first boot (which registers the nodeIds resource), then restarts OpcServer to load the acl.
				Soak::GrantWriteRights( client );
				exitCode = EXIT_SUCCESS;
			}
			else
				exitCode = Soak::Run( client );
		}
	}
	catch( runtime_error& e ){
		exitCode = Process::ExitException( move(e) );
	}
	Process::Shutdown( exitCode );
	return exitCode;
}

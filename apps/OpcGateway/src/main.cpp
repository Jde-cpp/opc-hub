#include <jde/fwk.h>
#include <jde/fwk/process/process.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/uatypes/Logger.h>
#include "GatewayAppClient.h"
#include "gatewayStartup.h"

#define let const auto
#ifndef _MSC_VER
	α Jde::Process::ProductName()ι->sv{ return "OpcGateway"; }
#endif

α main( int argc, char **argv )->int{
	using namespace Jde;
	Logging::AddTagParser( mu<Opc::UALogParser>() );
	int exitCode{ EXIT_FAILURE };
	try{
		Process::Startup( argc, argv, "Jde.OpcGateway", "IOT Connection" );
		Opc::Gateway::AppClient()->InitLogging( Opc::Gateway::AppClient() );
		let webServerSettings = Settings::FindObject( "/http" );
		let userName = Settings::FindObject( "/credentials" );
		Opc::Gateway::Startup( webServerSettings ? *webServerSettings : jobject{}, userName ? *userName : jobject{} );
		exitCode = Process::Pause();
	}
	catch( runtime_error& e ){
		exitCode = Jde::Process::ExitException( move(e) );
	}
	Process::Shutdown( exitCode );
	return exitCode;
}
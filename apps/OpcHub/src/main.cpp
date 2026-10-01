#include <jde/fwk.h>
#include <jde/fwk/process/process.h>
#include <jde/opc/uatypes/Logger.h>
#include "../../AppServer/src/appStartup.h"
#include "hubStartup.h"
#include <jde/fwk/process/cpu.h>

#define let const auto
#ifndef _MSC_VER
	α Jde::Process::ProductName()ι->sv{ return "OpcHub"; }//$(PRODUCT_NAME): the ProgramData/cert tree - on windows the .rc's ProductName.
#endif

α main( int argc, char **argv )->int{
	if( !Jde::Process::CheckCpu() )
		return EXIT_FAILURE;
	using namespace Jde;
	Logging::AddTagParser( mu<Opc::UALogParser>() );//the gateway's ua* tag names, before the settings' tag levels are parsed.
	int exitCode{ EXIT_FAILURE };
	try{
		Process::Startup( argc, argv, "Jde.OpcHub", "Jde OPC hub - AppServer + OpcGateway in one process." );
		App::Server::InitLogging();//the one Logging::Init: the AppServer's (ProtoLog, no RemoteLog - the process would forward its log to itself).
		let credentials = Settings::FindObject( "/credentials" );
		Opc::Hub::Startup( Settings::AsObject("/http"), credentials ? *credentials : jobject{} );
		exitCode = Process::Pause();
	}
	catch( runtime_error& e ){
		exitCode = Process::ExitException( move(e) );
	}
	Process::Shutdown( exitCode );
	return exitCode;
}

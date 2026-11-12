#include <jde/fwk.h>
#include <jde/fwk/process/process.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/opc/openProtocol/logTags.h>
#include "../../AppServer/src/appStartup.h"
#include "hubStartup.h"
#include <jde/fwk/process/cpu.h>

#define let const auto
#ifndef _MSC_VER
	α Jde::Process::ProductName()ι->sv{ return "OpcHub"; }//$(PRODUCT_NAME): the ProgramData/cert tree - on windows the .rc's JdeProductName.
#endif

α main( int argc, char **argv )->int{
	if( !Jde::Process::CheckCpu() )
		return EXIT_FAILURE;
	using namespace Jde;
	Logging::AddTags( Opc::TagNames );//the gateway's ua* tag names, before the settings' tag levels are parsed.
	Logging::AddTags( Opc::OpenProtocol::TagNames );//the gateway library's Open Protocol sessions log under it.
	int exitCode{ EXIT_FAILURE };
	try{
		Process::Startup( argc, argv, "Jde.OpcHub", "Jde OPC hub - AppServer + OpcGateway in one process." );
		App::Server::InitLogging();//the one Logging::Init: the AppServer's (ProtoLog, no RemoteLog - the process would forward its log to itself).
		Opc::Hub::Startup( Settings::AsObject("/http") );
		exitCode = Process::Pause();
	}
	catch( runtime_error& e ){
		exitCode = Process::ExitException( move(e) );
	}
	Process::Shutdown( exitCode );
	return exitCode;
}
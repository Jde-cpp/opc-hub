#include "appStartup.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/process/cpu.h>

namespace Jde{
#ifndef _MSC_VER
	α Process::CompanyName()ι->string{ return "Jde-Cpp"; }
	α Process::ProductName()ι->sv{ return "AppServer"; }
#endif

	α startup( int argc, char** argv )ε->void{
		using namespace Jde::App::Server;
		Process::Startup( argc, argv, "Jde.AppServer", "jde-cpp App Server." );
		App::Server::InitLogging();
		auto settings = Settings::FindObject( "/http" );
		AppStartup( settings ? move(*settings) : jobject{} );
	}
}

α main( int argc, char** argv )->int{
	if( !Jde::Process::CheckCpu() )
		return EXIT_FAILURE;
	using namespace Jde;
	int exitCode;
	try{
		startup( argc, argv );
		exitCode = Process::Pause();
	}
	catch( runtime_error& e ){
		exitCode = Process::ExitException( move(e) );
	}
	Process::Shutdown( exitCode );
	return exitCode;
}
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/app/log/ProtoLog.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.App"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, Process::ProductName(), "App shared-library tests", true );
		Logging::Init();
		App::ProtoLog::Init();//the `binary` logger LogSettingsTests reads and updates.  A no-op unless /logging/proto is set.
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	::testing::InitGoogleTest( &argc, argv );
	int exitCode{ EXIT_FAILURE };
	try{
		startup( argc, argv );
		ApplyTestsFilter();
		exitCode = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( runtime_error& e ){
		exitCode = StartupFailed( e );//never the exception's code:  main's status keeps only its low 8 bits.
	}
	Process::Shutdown( exitCode );
	return exitCode;
}

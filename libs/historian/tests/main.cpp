//libs/historian's own unit suite.  Everything runs on a ManualClock against an in-memory library - no server, no data
//source, and no waiting for midnight, delay or a heartbeat.  The fixtures in hosts.h are the two shapes the library
//serves: OpcServer's single group and the gateway's many.
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Opc.Historian"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, "Tests.Opc.Historian", "Jde.Opc.HistorianLib unit tests", true );
		Logging::Init();
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	let filterSet = Process::Args().find( "--gtest_filter" )!=Process::Args().end();
	::testing::InitGoogleTest( &argc, argv );
	int exitCode{ EXIT_FAILURE };
	try{
		startup( argc, argv );
		if( !filterSet )
			::testing::GTEST_FLAG( filter ) = Settings::FindSV( "/testing/tests" ).value_or( "*" );
		exitCode = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( runtime_error& e ){
		exitCode = StartupFailed( e );
	}
	Process::Shutdown( exitCode );
	return exitCode;
}
#include "gtest/gtest.h"
#include <jde/fwk/process/thread.h>
#include <jde/fwk/log/log.h>
#include <jde/tests/SpdlogTestListener.h>
#include <jde/tests/testMain.h>
#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Web"; }
#endif

	Ω startup( int argc, char **argv )ε->void{
		Thread::SetName( "Main" );
		Process::Startup( argc, argv, "Tests.Web", "Web tests", true );
		Logging::Init();
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	::testing::InitGoogleTest( &argc, argv );
	int exitCode{ EXIT_FAILURE };
	try{
		startup( argc, argv );
		ApplyTestsFilter();
		Jde::SpdlogTestListener::Config( ::testing::UnitTest::GetInstance()->listeners() );
		exitCode = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( runtime_error& e ){
		exitCode = StartupFailed( e );//never the exception's code:  main's status keeps only its low 8 bits.
	}
	Process::Shutdown( exitCode );
	return exitCode;
}
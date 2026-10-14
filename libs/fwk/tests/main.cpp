#include "gtest/gtest.h"
#include <jde/fwk/settings.h>
#include <jde/tests/testMain.h>
#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Framework"; }
#endif
 	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, "Tests.Framework", "Unit Tests description", true );
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
		INFOT( ELogTags::App, "filter:'{}'", ::testing::GTEST_FLAG(filter) );
		exitCode = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( runtime_error& e ){
		exitCode = StartupFailed( e );//never the exception's code:  main's status keeps only its low 8 bits.
	}
	Process::Shutdown( exitCode );
	return exitCode;
}
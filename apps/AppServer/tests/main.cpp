#include "gtest/gtest.h"
#include <jde/fwk/settings.h>
#include <jde/tests/SpdlogTestListener.h>
#include <jde/tests/testMain.h>
#include "../src/appStartup.h"
#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.AppServer"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, "Tests.AppServer", "AppServer unit tests", true );
		App::Server::InitLogging();
		App::Server::AppStartup( Settings::AsObject("/http/app") );
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	::testing::InitGoogleTest( &argc, argv );
	int result{ EXIT_FAILURE };
	try{
		startup( argc, argv );
		ApplyTestsFilter();
		Jde::SpdlogTestListener::Config( ::testing::UnitTest::GetInstance()->listeners() );
		result = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( std::exception& e ){
		if( auto p = dynamic_cast<Exception*>( &e ); p )
			p->Log();
		Process::ExitException( move(e) );
	}
	Process::Shutdown( result );

	return result;
}

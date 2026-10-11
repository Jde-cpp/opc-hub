//Jde.Opc's own unit suite:  the uatypes wrappers and their round trips - ToJson/FromJson, ToString/DecodeJson,
//Variant<->Proto::Value - over every identifier kind and array shape.  Nothing here starts a server, opens a socket or
//touches a data source; apps/OpcGateway/tests (confusingly, the target named Jde.Opc.Tests) is the integration suite.
//
//Every test here is enabled:  the DISABLED_R2_<n>_* convention this comment used to describe is gone, along with the
//findings it was waiting on.  A test that names a finding is its acceptance check, not a placeholder.
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Opc.Lib"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Logging::AddTagParser( mu<Opc::UALogParser>() ); //before Startup:  the jsonnet names opc tags, and UALogParserTests asserts they resolve.
		Process::Startup( argc, argv, "Tests.Opc.Lib", "Jde.Opc uatypes unit tests", true );
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
		exitCode = StartupFailed( e );//never the exception's code:  main's status keeps only its low 8 bits.
	}
	Process::Shutdown( exitCode );
	return exitCode;
}

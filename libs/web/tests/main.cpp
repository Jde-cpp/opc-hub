#include "gtest/gtest.h"
#include <jde/fwk/process/thread.h>
#include <jde/fwk/log/log.h>
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

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup ); }
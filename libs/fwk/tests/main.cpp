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

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup, false ); }
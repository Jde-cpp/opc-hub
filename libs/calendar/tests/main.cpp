//Jde.CalendarLib's own unit suite.  No server and no data source:  each test builds a calendar from the json the table QL
//would return and asserts instants in America/New_York, across both of its clock changes.
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Calendar"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, "Tests.Calendar", "Jde.CalendarLib unit tests", true );
		Logging::Init();
	}
}

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup, false ); }
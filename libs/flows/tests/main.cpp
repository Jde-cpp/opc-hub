//Jde.Opc.FlowLib's own unit suite.  No server, no data source and no files:  each test deploys a graph over the suite's
//host, whose sources and sinks it scripts, on a ManualClock, and reads what the sinks took and what the trace kept.
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Opc.Flow"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, "Tests.Opc.Flow", "Jde.Opc.FlowLib unit tests", true );
		Logging::Init();
	}
}

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup, false ); }
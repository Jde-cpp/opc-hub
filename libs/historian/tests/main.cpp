//libs/historian's own unit suite.  No server and no data source:  each test writes real files, under hist-tests beside
//the logs, which its fixture removes.  Nearly every test runs on a ManualClock, so none waits for midnight, delay or a
//heartbeat; SystemClockFlushes is the one on the system clock.  A flush's writes complete on the process's executor, so
//a test waits for the flush itself.  The fixtures in hosts.h are the two shapes the library serves: OpcServer's single
//group and the gateway's many.
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

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup, false ); }
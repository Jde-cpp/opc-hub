//Jde.Opc.OeeLib's own unit suite.  No server, no data source and no historian files:  each test scripts the records a
//group's subscription would deliver and replays them through an engine on a ManualClock, or hands the formulas rows it
//wrote out by hand.
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Opc.Oee"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, "Tests.Opc.Oee", "Jde.Opc.OeeLib unit tests", true );
		Logging::Init();
	}
}

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup, false ); }
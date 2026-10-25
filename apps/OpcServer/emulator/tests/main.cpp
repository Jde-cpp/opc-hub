//The PLC emulator's unit suite (emulator-review T2):  Signals - TagSpec parsing and every generator - ParseDevices,
//and PlcServer, the emulated PLC's own UA server on the library's endpoint.  Nothing here logs into an AppServer or
//opens a session on an OpcServer; PlcServer binds loopback on a test port with an app client that has no hub, so its
//logins are the block's username, and its writer sends UADP to a port nothing listens on.
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Opc.PlcEmulator"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Logging::AddTagParser( mu<Opc::UALogParser>() );//before Startup: the jsonnet names opc tags.
		Process::Startup( argc, argv, "Tests.Opc.PlcEmulator", "PLC emulator unit tests", true );
		Logging::Init();
	}
}

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup ); }
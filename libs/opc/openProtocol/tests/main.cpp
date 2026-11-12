//Jde.Opc.OpenProtocol's own unit suite.  No server and no data source:  the codec's tests decode a byte buffer or
//encode a message, the buffers written from the specification's tables (Atlas Copco 9836 4415 01, Release 2.20.1), and
//the session's run it against the controller's side of the codec on the executor, over loopback.
#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/opc/openProtocol/logTags.h>
#include <jde/tests/testMain.h>

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Opc.OpenProtocol"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Logging::AddTags( Opc::OpenProtocol::TagNames ); //before Startup:  the jsonnet names the tag.
		Process::Startup( argc, argv, "Tests.Opc.OpenProtocol", "Jde.Opc.OpenProtocol unit tests", true );
		Logging::Init();
	}
}

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup, false ); }
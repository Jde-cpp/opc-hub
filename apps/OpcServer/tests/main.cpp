#include "gtest/gtest.h"
#include <jde/fwk/settings.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/io/json.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/tests/testMain.h>
#include "../src/opcServerStartup.h"
#include "../../AppServer/src/appStartup.h"
#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.OpcServer"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Logging::AddTagParser( mu<Opc::UALogParser>() );
		Process::Startup( argc, argv, "Tests.OpcServer", "OpcServer tests", true );
		Opc::Server::AppClient()->InitLogging( Opc::Server::AppClient() );
		if( Settings::FindBool("/testing/embeddedAppServer").value_or(true) )//the fresh db enrolls the client cert every run: /access/trustedCertDirs anchors its dir, Opc::Server::Startup ensures the cert, and TrustVerify rescans - no pre-anchoring of the CLIENT cert here.  The other direction (client trusts each embedded server's cert) is covered by Web::Server::Start's self-anchor.
			App::Server::AppStartup( Settings::AsObject("/http/app") );
		Opc::Server::Startup( Settings::AsObject("/http/opcServer") );
	}
}

α main( int argc, char **argv )->int{ return Jde::Tests::Main( argc, argv, Jde::startup ); }
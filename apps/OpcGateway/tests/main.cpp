#include "gtest/gtest.h"
#include <exception>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/io/json.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/uatypes/Logger.h>
#include "../src/GatewayAppClient.h"
#include "../src/gatewayStartup.h"
#include "../src/UAClient.h"
#include "../../AppServer/src/appStartup.h"
#include "../../OpcServer/src/opcServerStartup.h"
#include "utils/helpers.h"
#include <jde/tests/SpdlogTestListener.h>
#include <jde/tests/testMain.h>
#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.Opc"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Logging::AddTagParser( mu<Opc::UALogParser>() );
		Process::Startup( argc, argv, "Tests.Opc", "Opc tests", true );
		Opc::Gateway::AppClient()->InitLogging( Opc::Gateway::AppClient() );
		if( Settings::FindBool("/testing/embeddedAppServer").value_or(true) )//the fresh db enrolls the gateway+opcServer client certs every run: /access/trustedCertDirs anchors their dirs, each startup ensures its own cert, and TrustVerify rescans - no pre-anchoring of the CLIENT certs here.  The other direction (client trusts each embedded server's cert) is covered by Web::Server::Start's self-anchor.
			App::Server::AppStartup( Settings::AsObject("/http/app") );
		if( Settings::FindBool("/testing/embeddedOpcServer").value_or(true) ){
			//create both gateway certs (UAClient transport + AppClient SslSettings auth) before the server: not required
			//anymore (UATrust rescans trustedCertDirs on a failed verify), but it spares the first connect a
			//fail-rescan-retry cycle and still matters for embeddedOpcServer=false against a snapshotting server.
			Opc::Gateway::UAClient::EnsureCertificate( Opc::Gateway::Tests::OpcServerSlug );
			Crypto::CryptoSettings sslSettings{ Json::FindDefaultObject(Settings::AsObject("/http/gateway"), "ssl"), {} };
			Crypto::EnsureKeyCertificate( sslSettings );
			Opc::Server::Startup( Settings::AsObject("/http/opcServer"), Settings::AsObject("/credentials/opcServer") );
		}
		Opc::Gateway::Startup( Settings::AsObject("/http/gateway"), Settings::AsObject("/credentials/gateway") );
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	::testing::InitGoogleTest( &argc, argv );
	int result{ EXIT_FAILURE };
	try{
		startup( argc, argv );
		::testing::GTEST_FLAG( filter ) = Settings::FindString( "/testing/tests" ).value_or( "*" );
		Jde::SpdlogTestListener::Config( ::testing::UnitTest::GetInstance()->listeners() );
		result = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( std::exception& e ){
		if( auto p = dynamic_cast<Exception*>( &e ); p )
			p->Log();
		Process::ExitException( move(e) );
	}
	Process::Shutdown( result );

	return result;
}

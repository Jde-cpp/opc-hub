#include "gtest/gtest.h"
#include <exception>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/opc/uatypes/Logger.h>
#include "../../AppServer/src/appStartup.h"
#include "../../OpcGateway/src/UAClient.h"
#include "../../OpcGateway/tests/utils/helpers.h"
#include "../../OpcServer/src/opcServerStartup.h"
#include "../src/hubStartup.h"
#include <jde/tests/SpdlogTestListener.h>
#include <jde/tests/testMain.h>
#define let const auto

//The hub exe's main (apps/OpcHub/src/main.cpp) with an embedded OpcServer for the OPC path - the shape of Jde.Opc.Tests' main.
namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.OpcHub"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Logging::AddTagParser( mu<Opc::UALogParser>() );
		Process::Startup( argc, argv, "Tests.OpcHub", "OpcHub tests", true );
		App::Server::InitLogging();//as the hub: the AppServer's - the one Logging::Init in the process.
		if( Settings::FindBool("/testing/embeddedOpcServer").value_or(true) ){
			//the gateway role's OPC client cert before the OpcServer starts - spares the first connect a fail-rescan-retry cycle (Jde.Opc.Tests does the same).
			Opc::Gateway::UAClient::EnsureCertificate( Opc::Gateway::Tests::OpcServerSlug );
			Crypto::CryptoSettings sslSettings{ Json::FindDefaultObject(Settings::AsObject("/http"), "ssl"), {} };
			Crypto::EnsureKeyCertificate( sslSettings );
		}
		//the OpcServer once the hub's listener is up - it logs in to the AppServer role over the socket.
		auto opcServer = Settings::FindBool("/testing/embeddedOpcServer").value_or(true) ? function<void()>{ []{ Opc::Server::Startup( Settings::AsObject("/http/opcServer"), Settings::AsObject("/credentials/opcServer") ); } } : function<void()>{};
		Opc::Hub::Startup( Settings::AsObject("/http"), Settings::AsObject("/credentials"), move(opcServer) );
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

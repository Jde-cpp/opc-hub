#include <jde/fwk.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/app/client/IAppClient.h>
#include <jde/app/client/appClient.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/ql/ql.h>
#include "EmulatorAppClient.h"
#include "Emulator.h"
#include <jde/fwk/process/cpu.h>

#define let const auto
#ifndef _MSC_VER
	α Jde::Process::ProductName()ι->sv{ return "PlcEmulator"; }
#endif

//PLC emulator for the Jde OpcServer.  A real UA-enabled PLC: its own OPC UA server holding the pumps nodeset, which it
//PUBLISHES over OPC UA PubSub (UADP/UDP) into the OpcServer's matching nodes, plus a client session to the OpcServer
//for the run commands the UI writes (and, with -transport=write, for writing the process values directly).
//
//First run:  -createCert (both certs; the UA one must sit in a dir the OpcServer trusts - see /access/trustedCertDirs),
//then -grant once OpcServer has booted (it registers the nodeIds resource), then RESTART OpcServer so it loads the acl.
α main( int argc, char **argv )->int{
	if( !Jde::Process::CheckCpu() )
		return EXIT_FAILURE;
	using namespace Jde;
	using namespace Jde::Opc;
	Logging::AddTagParser( mu<Opc::UALogParser>() );
	int exitCode{ EXIT_FAILURE };
	try{
		Process::Startup( argc, argv, "Jde.Opc.PlcEmulator", "PLC emulator for the Jde OpcServer" );
		if( Process::FindArg("-createCert") ){
			Emulator::CreateCertificates();
			exitCode = EXIT_SUCCESS;
		}
		else{
			//`logSetting` names no view here (this process has no schema at all), so without it QL::Parse refuses the
			//AppServer's log-level push at the parser - "Could not find view 'log_settings'" - before EmulatorAppClient
			//ever sees it.  The OpcServer and the gateway register the same name for the same reason.
			QL::SetSystemTables( {"logSetting"} );
			auto client = Emulator::AppClient();
			client->InitLogging( client );
			Crypto::CryptoSettings ssl{ Json::FindDefaultObject(Settings::AsObject("/http"), "ssl"), Process::ProductName() };//the AppServer login cert - the file name doubles as the subject CN.
			Crypto::EnsureKeyCertificate( ssl );
			client->SslSettings = ssl;//no SetUserName:  the login is by this key, and IAppClient::UserName() has no reader anyway.
			Execution::Run();//nothing else starts the executor thread the AppServer socket needs.
			BlockVoidAwait( App::Client::ConnectAwait{client, false} );
			if( Process::FindArg("-grant") ){
				Emulator::GrantWriteRights( client );
				exitCode = EXIT_SUCCESS;
			}
			else
				exitCode = Emulator::Run( client );
		}
	}
	catch( runtime_error& e ){
		exitCode = Process::ExitException( move(e) );
	}
	Process::Shutdown( exitCode );
	return exitCode;
}

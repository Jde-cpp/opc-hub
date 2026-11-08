#include <jde/fwk/process/process.h>
#include <cstdlib>
#include <iostream>
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
	#include <absl/debugging/failure_signal_handler.h>
	#include <absl/debugging/symbolize.h>
#endif
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>

#define let const auto

namespace Jde{
	constexpr ELogTags _tags = ELogTags::App;
	static string _applicationName;
	α Process::AppName()ι->const string&{ return _applicationName; }

	static bool _isConsole{};
	α Process::IsConsole()ι->bool{ return _isConsole; }


	static TimePoint _startTime{ Clock::now() };
	α Process::StartTime()ι->TimePoint{ return _startTime; };
}
namespace Jde{
	//A SIGSEGV, SIGABRT, SIGBUS, SIGILL or SIGFPE prints the signal and a symbolized stack to stderr - the journal, under systemd -
	//then re-raises, so the core dump and exit status are unchanged.  Installed before AddSignals, whose SIGTERM handler replaces
	//abseil's:  SIGTERM is the graceful stop here.  Not under ASan, which has its own reporter; not on Windows, where WER has it.
	Ω installFailureHandler( [[maybe_unused]] const char* argv0 )ι->void{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
		absl::InitializeSymbolizer( argv0 );
		absl::InstallFailureSignalHandler( {} );
#endif
	}
#undef SetConsoleTitle
	//What re-runs a suite by hand:  the environment and directory the harness gave it, then its command line.
	Ω printRerun( str commandLine )ι->void{
		std::cout
#if _WIN32
			<< "export REPO_BUILD_DIR=\"" << Process::GetEnv("REPO_BUILD_DIR").value_or("") << "\"" << std::endl
			<< "export REPO_SOURCE_DIR=\"" << Process::GetEnv("REPO_SOURCE_DIR").value_or("") << "\"" << std::endl
#else
			<< "REPO_BUILD_DIR=" << Process::GetEnv("REPO_BUILD_DIR").value_or("") << std::endl
			<< "REPO_SOURCE_DIR=" << Process::GetEnv("REPO_SOURCE_DIR").value_or("") << std::endl
#endif
			<< "@" << fs::current_path().string() << std::endl
			<< commandLine << std::endl;
	}
	Ω logCommandLine( int argc, char** argv )ε->void{
		std::ostringstream commandLine;
		for( auto i=0; i<argc; ++i )
			commandLine << argv[i] << " ";
		if( Process::FindArg("-tests") || Process::FindArg("-ctest") )
			printRerun( commandLine.str() );
		INFO( "Program: {}", commandLine.str() );
		INFO( "pid/cwd: ({}){}", Process::ProcessId(), fs::current_path().string() );
	}
	α Process::Startup( int argc, char** argv, sv appName, string serviceDescription, optional<bool> console )ε->void{
		vector<string> tokens;
		for( int i=1; i<argc; ++i )
			tokens.emplace_back( argv[i] );
		let args = ParseStartupArgs( tokens, console );
		_isConsole = args.Console;
		_applicationName = appName;
		if( args.SetTerminate )
			std::set_terminate( OnTerminate );
		installFailureHandler( argc ? argv[0] : nullptr );
		logCommandLine( argc, argv );
		if( args.Install ){
			Install( serviceDescription, args.ServiceArgs );
			throw Exception{ "successfully installed.", ELogLevel::Trace };
		}
		if( args.Uninstall ){
			Uninstall();
			throw Exception{ "successfully uninstalled.", ELogLevel::Trace };
		}
		if( args.Console )
			Process::SetConsoleTitle( appName );
		else if( !AsService() )//unconnected, it would run on as a service the SCM has given up on and cannot stop
			throw Exception{ "Could not start as a service." };
		Thread::SetName( appName );
		Process::AddSignals();
		IO::Init();//after AsService:  its ring and eventfd are the daemon's, and a failure here reaches the SCM.
		Execution::Start();
	}
	α Process::AppDataFolder()ι->fs::path{
		return ProgramDataFolder()/CompanyRootDir()/Process::ProductName();
	}
	α Process::GetEnv( str variable, bool emptyIsNullOpt )ι->optional<string>{
		optional<string> y;
#ifdef _WIN32
		char* env = nullptr;
		uint size = 0;
		if( _dupenv_s(&env, &size, variable.c_str()) == 0 && env ){
			y = string{ env };
			free( env );
		}
#else
		if( let env = std::getenv(variable.c_str()); env )
			y = string{ env };
#endif
		return emptyIsNullOpt && y && y->empty() ? optional<string>{} : y;
	}
}
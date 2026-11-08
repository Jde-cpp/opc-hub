#pragma once
#ifndef JDE_APP_H
#define JDE_APP_H
#include <absl/functional/any_invocable.h>

#define Φ Γ α
namespace Jde{
	struct IShutdown{
		β Shutdown( bool terminate, SRCE )ι->void=0;
		virtual ~IShutdown() = default;
	};
namespace Process{
	Φ AppName()ι->const string&;
	Φ AppDataFolder()ι->fs::path;
	Φ Args()ι->const flat_multimap<string,string>&;
	Φ ParseArgs( const vector<string>& tokens )ι->flat_multimap<string,string>;
	Φ FindArg( string key )ι->optional<string>;
	Φ CompanyName()ι->string;
	Φ CompanyRootDir()ι->fs::path;
	Φ GetEnv( str variable, bool emptyIsNullOpt=true )ι->optional<string>;
	Φ Executable()ι->fs::path;
	Φ ExePath()ι->fs::path;
	Φ HostName()ι->string;
	/// How the process was launched. ie not a service.  Says nothing about where stdout actually goes.
	Φ IsConsole()ι->bool;
	Φ IsDebuggerPresent()ι->bool;
	Φ SetConsoleTitle( sv title )ι->void;
	Φ IsTerminal()ι->bool; // Where stdout goes.
	Φ PrepareConsole()ι->bool; // The terminal stdout is on, readied for the console sink:  whether it draws escape sequences (the dev pattern's links).
	Φ RaiseTimerResolution()ι->void; // For the process's life, to 1 ms:  windows otherwise stretches a shorter wait to its 15.6 ms tick.  A no-op on linux.
	Φ MemorySize()ι->uint;
	Φ ProcessId()ι->uint32;
	constexpr static sv ProductVersion="2026.02.01";
	Φ ProgramDataFolder()ι->fs::path;
	Φ ProductName()ι->sv;
	Φ StartTime()ι->TimePoint;

	//ServiceArgs:  what a service registered by -install starts with, in argv order - everything but -c, -t, -install and -uninstall.
	struct StartupArgs{ bool Console{}; bool SetTerminate{}; bool Install{}; bool Uninstall{}; vector<string> ServiceArgs; };
	Φ ParseStartupArgs( const vector<string>& tokens, optional<bool> console )ι->StartupArgs;
	Φ Startup( int argc, char** argv, sv appName, string serviceDescription, optional<bool> console=nullopt )ε->void;
	Φ AddSignals()ε->void;
	Φ AsService()ι->bool;
	Φ Pause()ι->int;

	Φ LoadLibrary( const fs::path& path )ε->void*;
	Φ FreeLibrary( void* p )ι->void;
	Φ GetProcAddress( void* pModule, str procName )ε->void*;

	Φ AddApplicationLog( ELogLevel level, str value )ι->void;
	Φ Kill( uint processId )ι->bool;

	Φ AddShutdownFunction( function<void(bool terminating, SL)>&& shutdown )ι->void;
	//Run by Shutdown after the executor's threads have joined - no work can still be running - and before the loggers go:  for what
	//has to follow the last write, e.g. closing a database (reviews/m4-closing.md #24).
	Φ AddFinalizeFunction( function<void(bool terminating)>&& finalize )ι->void;
	//Run by Shutdown last, after the loggers and the io_context have gone - the process holds nothing now:  for telling the outside
	//it has ended, e.g. a service's stopped.  No logging.
	Φ AddExitFunction( absl::AnyInvocable<void() const>&& exit )ι->void;

	Φ AddShutdown( IShutdown* pShutdown )ι->void; //global unique ptrs
	Φ RemoveShutdown( IShutdown* pShutdown )ι->void;

	Φ ExitException( std::exception&& e )ι->int;
	Φ ExitReason()ι->optional<int>;//the exit code the first cause asked for.
	Φ OnTerminate()ι->void;
	α SetExitReason( int exitCode, bool terminate )ι->void;//first cause wins.  Safe in a signal handler.
	Φ Shutdown( int exitCode )ι->void;
	Φ ShuttingDown()ι->bool;
	Φ LoggersGone()ι->bool;

	//The service's ImagePath:  the exe, always quoted, then `args` escaped per CommandLineToArgvW - the parser on the other
	//end (Args() on windows) - so what the SCM launches parses back to the same tokens.  Pure, split out of Install so it
	//can be asserted where there is no SCM.
	Φ ServiceCommandLine( const fs::path& exe, const vector<string>& args )ι->string;
	//`args`:  what the service starts with - Startup's unconsumed tokens (-settings, -include, -sync), never -install/-c/-t.
	Φ Install( str serviceDescription, const vector<string>& args )ε->void;
	Φ Uninstall()ε->void;
}}
#undef Φ
#endif
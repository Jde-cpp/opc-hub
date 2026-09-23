#pragma once
#ifndef JDE_APP_H
#define JDE_APP_H

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
	Φ SetConsole( bool isConsole )ι->void;
	Φ SetConsoleTitle( sv title )ι->void;
	Φ IsTerminal()ι->bool; // Where stdout goes.
	Φ SetExecutor( up<IShutdown>&& executor )ι->void;
	Φ MemorySize()ι->size_t;
	Φ ProcessId()ι->uint32;
	constexpr static sv ProductVersion="2026.02.01";
	Φ ProgramDataFolder()ι->fs::path;
	Φ ProductName()ι->sv;
	Φ StartTime()ι->TimePoint;

	Φ Startup( int argc, char** argv, sv appName, string serviceDescription, optional<bool> console=nullopt )ε->flat_set<string>;
	Φ AddSignals()ε->void;
	Φ AsService()ι->bool;
	Φ Pause()ι->int;
	Φ UnPause()ι->void;

	Φ LoadLibrary( const fs::path& path )ε->void*;
	Φ FreeLibrary( void* p )ι->void;
	Φ GetProcAddress( void* pModule, str procName )ε->void*;

	Φ AddApplicationLog( ELogLevel level, str value )ι->void;
	Φ Kill( uint processId )ι->bool;

	Φ AddShutdownFunction( function<void(bool terminating, SL)>&& shutdown )ι->void;
	//Run by Shutdown after the executor's threads have joined - no work can still be running - and before the loggers go:  for what
	//has to follow the last write, e.g. closing a database (reviews/m4-closing.md #24).
	Φ AddFinalizeFunction( function<void(bool terminating)>&& finalize )ι->void;

	Φ AddShutdown( IShutdown* pShutdown )ι->void; //global unique ptrs
	Φ RemoveShutdown( IShutdown* pShutdown )ι->void;

	Φ ExitException( std::exception&& e )ι->int;
	Φ ExitHandler( int s )->void;
	Φ ExitReason()ι->optional<int>;
	Φ OnTerminate()ι->void;
	Φ SetExitReason( int reason, bool terminate )ι->void;
	Φ Shutdown( int exitReason )ι->void;
	Φ ShuttingDown()ι->bool;
	Φ Finalizing()ι->bool;

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
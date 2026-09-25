#include <jde/fwk/process/process.h>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <typeinfo>
#include <sys/types.h>

#include <jde/fwk/settings.h>
#include <jde/fwk/io/Cache.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/utils/Vector.h>

#define let const auto

namespace Jde{
	constexpr ELogTags _tags = ELogTags::App;
	string _applicationName;
	α Process::AppName()ι->const string&{ return _applicationName; }

	bool _isConsole{};
	α Process::SetConsole( bool value )ι->void{ _isConsole=value; }
	α Process::IsConsole()ι->bool{ return _isConsole; }


	TimePoint _startTime{ Clock::now() };
	α Process::StartTime()ι->TimePoint{ return _startTime; };

	function<void()> OnExit;
}
namespace Jde{
	α Process::ParseArgs( const vector<string>& tokens )ι->flat_multimap<string,string>{
		flat_multimap<string,string> y;
		optional<string> key; //flag awaiting a value: `-k v` binds on the next token, `-k` alone binds empty.
		for( let& current : tokens ){
			if( !current.starts_with('-') ){ //the pending flag's value, or a positional (empty key).
				y.emplace( key ? move(*key) : string{}, current );
				key.reset();
				continue;
			}
			if( key ) //previous flag never got a value.
				y.emplace( move(*key), string{} );
			key.reset(); //every path that consumes key resets it - reading a moved-from optional emplaced junk.
			if( uint i=current.find('='); i<current.size() ){
				auto value = current.substr( i+1 );
				if( value.size()>1 && value.front()=='"' && value.back()=='"' ) //lldb/VS Code pass argv unshelled, so the quotes are literal.
					value = value.substr( 1, value.size()-2 );
				y.emplace( current.substr(0, i), move(value) );
			}
			else
				key = current;
		}
		if( key )
			y.emplace( move(*key), string{} );
		return y;
	}

	α Process::FindArg( string key )ι->optional<string>{
		auto p = Args().find( key );
		return p!=Args().end() ? p->second : optional<string>{};
	}

	//CommandLineToArgvW's rules, since that is what reads a service's ImagePath back (Args()):  a backslash is literal
	//unless it precedes a '"', where the run doubles and the quote is escaped; a run before the closing quote doubles too.
	//Only tokens that need it are quoted - a plain -settings=C:\ProgramData\… stays readable in `sc qc`.
	Ω quoteArg( sv arg, bool force )ι->string{
		if( !force && !arg.empty() && arg.find_first_of(" \t\"")==sv::npos )
			return string{ arg };
		string y{ '"' };
		uint backslashes{};
		for( char c : arg ){
			if( c=='\\' ){
				++backslashes;
				continue;
			}
			y.append( c=='"' ? backslashes*2+1 : backslashes, '\\' );
			backslashes = 0;
			y += c;
		}
		y.append( backslashes*2, '\\' );
		return y += '"';
	}
	α Process::ServiceCommandLine( const fs::path& exe, const vector<string>& args )ι->string{
		//The exe is quoted unconditionally:  unquoted, `C:\Program Files\…\x.exe` parsed back as argv[0]=`C:\Program`, which
		//Executable() handed to loadResource - ProductName() then fell back to "Jde-cpp" and the service grew a second data tree.
		string y = quoteArg( exe.string(), true );
		for( const auto& arg : args )
			y += ' ' + quoteArg( arg, false );
		return y;
	}
}
namespace Jde{
#undef SetConsoleTitle
	α Process::Startup( int argc, char** argv, sv appName, string serviceDescription, optional<bool> console )ε->flat_set<string>{
		auto isConsole = console ? *console : Process::FindArg( "-c" ).has_value();
		Process::SetConsole( isConsole );
		IO::Init();
		{
			std::ostringstream args;
			for( auto i=0; i<argc; ++i )
				args << argv[i] << " ";
			if( FindArg("-tests") || FindArg("-ctest") ){
				std::cout
#if _WIN32
					<< "export REPO_BUILD_DIR=\"" << GetEnv("REPO_BUILD_DIR").value_or("") << "\"" << std::endl
     			<< "export REPO_SOURCE_DIR=\"" << GetEnv("REPO_SOURCE_DIR").value_or("") << "\"" << std::endl
#else
					<< "REPO_BUILD_DIR=" << GetEnv("REPO_BUILD_DIR").value_or("") << std::endl
					<< "REPO_SOURCE_DIR=" << GetEnv("REPO_SOURCE_DIR").value_or("") << std::endl
#endif
					<< "@" << fs::current_path().string() << std::endl
					<< args.str() << std::endl;
			}
			INFO( "Program: {}", args.str() );
			INFO( "pid/cwd: ({}){}", ProcessId(), fs::current_path().string() );
		}
		_applicationName = appName;
		const string arg0{ argv[0] };
		bool terminate = !_debug;
		bool install{}, uninstall{};
		flat_set<string> values;
		vector<string> serviceArgs; //the tokens this loop leaves alone, in argv order:  what a service registered by -install starts with (-settings, -include, -sync).
		for( int i=1; i<argc; ++i ){
			if( string(argv[i])=="-c" && !console )
				isConsole = true;
			else if( string(argv[i])=="-t" )
				terminate = !terminate;
			else if( string(argv[i])=="-install" )
				install = true;
			else if( string(argv[i])=="-uninstall" )
				uninstall = true;
			else{
				values.emplace( argv[i] );
				if( string(argv[i])!="-c" ) //console is this run's, not the service's.
					serviceArgs.emplace_back( argv[i] );
			}
		}
		//After the scan, not on the token:  acting on -install mid-loop meant a -settings that came after it never reached
		//the registration, and the service silently started on the default config search instead.
		if( install ){
			Install( serviceDescription, serviceArgs );
			throw Exception{ "successfully installed.", ELogLevel::Trace };
		}
		if( uninstall ){
			Uninstall();
			throw Exception{ "successfully uninstalled.", ELogLevel::Trace };
		}
		if( terminate )
			std::set_terminate( OnTerminate );
		if( isConsole )
			Process::SetConsoleTitle( appName );
		else
			AsService();
		Thread::SetName( appName );
		Process::AddSignals();
		Cache::Init();
		return values;
	}

	optional<int> _exitReason;
	bool _terminate{};
	α Process::ExitReason()ι->optional<int>{ return _exitReason; }
	α Process::SetExitReason( int reason, bool terminate )ι->void{ _exitReason = reason; _terminate = terminate; }
	α Process::ShuttingDown()ι->bool{ return (bool)_exitReason; }
	bool _finalizing{};
	α Process::Finalizing()ι->bool{ return _finalizing; }

	α Process::ExitException( std::exception&& e )ι->int{
		int y{ EXIT_FAILURE };
		string message;
		if( auto p = dynamic_cast<Exception*>(&e); p ){
			y = p->Level()==ELogLevel::Trace ? EXIT_SUCCESS :
				p->Code() ? ( int )p->Code() : EXIT_FAILURE;
			message = p->What();//What(), not what() - it forces the lazy format/args expansion.
			if( message.empty() ){//no format, no args, no inner - all that is left is where it came from.
				let& sl = p->Source();
				message = Ƒ( "[{}] no message, thrown at {}({}) '{}', code={:x}", typeid(*p).name(), sl.file_name(), sl.line(), sl.function_name(), p->Code() );
			}
		}
		else if( let what = e.what(); what && *what )
			message = what;
		if( message.empty() )//e.g. a default-constructed derived std::exception whose what() is ""
			message = Ƒ( "[{}] no message", typeid(e).name() );

		sv prefix = y==0 ? "" : "Exiting on error:  ";//a success is said bare, on stdout:  -install/-uninstall report theirs by throwing it, and "Exiting on exception:" read as a failure in Setup's details pane (reviews/install-issues.md #46).
		LOG( y==0 ? ELogLevel::Information : ELogLevel::Critical, ELogTags::App | ELogTags::Shutdown, "{}{}", prefix, message );
		if( !ExitReason() )
			SetExitReason( y, false );
		(y==0 ? std::cout : std::cerr) << prefix << message << std::endl;
		if( y!=EXIT_SUCCESS && !IsConsole() )//a service's stderr goes nowhere, and a startup failure never reaches the dispatcher:  the file log was its only record (reviews/m4-closing.md #10, run 4).
			AddApplicationLog( ELogLevel::Critical, Ƒ("{}{}", prefix, message) );
		return y;
	}

	vector<function<void( bool, SL )>> _shutdownFunctions;
	α Process::AddShutdownFunction( function<void(bool, SL)>&& shutdown )ι->void{
		_shutdownFunctions.push_back( shutdown );
	}

	vector<function<void(bool)>> _finalizeFunctions;
	α Process::AddFinalizeFunction( function<void(bool)>&& finalize )ι->void{
		_finalizeFunctions.push_back( move(finalize) );
	}

	up<IShutdown> _executor;
	α Process::SetExecutor( up<IShutdown>&& executor )ι->void{
		_executor = move( executor );
	}


	Vector<IShutdown*> _rawShutdowns;
	α Process::AddShutdown( IShutdown* shutdown )ι->void{
		ASSERT( !_rawShutdowns.find(shutdown) );
		_rawShutdowns.push_back( shutdown );
	}
	//No assert on absence: Shutdown() below drains the whole container before invoking anything, so by the time the
	//registrants are destroyed - which is where they unregister - there is nothing left to find.  That is the ordinary
	//path, not a bug.
	α Process::RemoveShutdown( IShutdown* shutdown )ι->void{
		_rawShutdowns.erase( shutdown );
	}


	Ω cleanup( bool terminate )ι->void;

	std::mutex _shutdownMutex;
	std::condition_variable _shutdownComplete;
	bool _shutdownFinished{};
	Ω shutdownTimeout()ι->Duration{
		if( let configured = Settings::FindDuration("/shutdown/timeout"); configured )
			return *configured;
		return Process::IsDebuggerPresent() ? Duration{5min} : Duration{30s};//stepping through teardown in a debugger is not a wedge - give it room, but still bound it.
	}
	//A process that runs its shutdown functions - closing its listeners - and then never returns from cleanup() is worse than
	//a hard exit: it keeps its AppServer registration and goes on advertising ports it has already closed.  Nothing stops new
	//work arriving mid-teardown (the 100ms sleep below is not a barrier), so this is the backstop.  reviews/gateway-review.md #33.
	Ω armShutdownWatchdog( int exitReason )ι->void{
		let timeout = shutdownTimeout();
		if( timeout<=Duration::zero() )
			return;
		std::thread{ [timeout,exitReason](){
			Thread::SetName( "ShutdownWatchdog" );
			std::unique_lock l{ _shutdownMutex };
			if( _shutdownComplete.wait_for(l, timeout, [](){return _shutdownFinished;}) )
				return;
			l.unlock();
			let message = Ƒ( "Shutdown did not complete within {}ms - exiting hard.", std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count() );
			std::cerr << message << std::endl;//the loggers are the likeliest thing wedged - say it somewhere that cannot be.
			Process::AddApplicationLog( ELogLevel::Critical, message );
			std::_Exit( exitReason );//not exit(): atexit handlers & static destructors are exactly what is stuck.
		} }.detach();
	}

	α Process::Shutdown( int exitReason )ι->void{
		static std::atomic_flag _ran;
		if( _ran.test_and_set() )//linux signal exits run this twice - Pause() calls it before returning, then main calls it with Pause's result; the 2nd pass re-ran the shutdown functions against already-nulled state (e.g. AppServer's AccessListener).
			return;
		if( !ExitReason() )//ExitHandler may have recorded the reason & terminate flag (SIGTERM) already - first cause wins.
			SetExitReason( exitReason, false );
		let terminate = _terminate;
		armShutdownWatchdog( ExitReason().value_or(exitReason) );

		for_each( _shutdownFunctions, [=](let& shutdown){shutdown(terminate, SRCE_CUR);} );
		DBGT( ELogTags::App | ELogTags::Shutdown, "{} Shutdown functions removed", _shutdownFunctions.size() );
		_shutdownFunctions.clear();
		_rawShutdowns.rerase( [=](auto& p){
			p->Shutdown( terminate );
		});
		DBGT( ELogTags::App | ELogTags::Shutdown, "Raw functions removed" );
		cleanup( terminate );
	}

	Ω cleanup( bool terminate )ι->void{
		INFOT( ELogTags::App, "Clearing Logger" );
		std::this_thread::sleep_for( 100ms );
		_finalizing = true;
		Cache::Shutdown( terminate );
		auto ioc = ExecutorIoc();//keep the io_context alive across teardown so it is destroyed last — after sessions, loggers and timers release their asio objects.
		if( _executor ){
			_executor->Shutdown( terminate );
			_executor = nullptr;//joins the executor's threads.
		}
		for_each( _finalizeFunctions, [=](let& finalize){finalize(terminate);} );
		_finalizeFunctions.clear();
		Logging::DestroyLoggers( terminate );
		if( ioc && ioc.use_count()>1 )//everything that used the io_context should have released it by now; a leftover ref means an asio object would otherwise outlive the io_context (use-after-free).
			std::cout << "WARNING: io_context still has " << ioc.use_count()-1 << " reference(s) at finalize." << std::endl;
		ioc = nullptr;//io_context destroyed here, deterministically last.
		{ lg _{_shutdownMutex}; _shutdownFinished = true; }
		_shutdownComplete.notify_all();//disarms the watchdog.
		std::cout << "Shutdown complete." << std::endl;
	}
	α Process::AppDataFolder()ι->fs::path{
		return ProgramDataFolder()/CompanyRootDir()/Process::ProductName();
	}
	α Process::GetEnv( str variable, bool emptyIsNullOpt )ι->optional<string>{
		optional<string> y;
#ifdef _WIN32
		char* env = nullptr;
		size_t size = 0;
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
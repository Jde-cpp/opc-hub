#include <iostream> // !important
#include <syslog.h>
#include <unistd.h>
#include <execinfo.h>
#include <dlfcn.h>

#include <jde/fwk/process/process.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/exceptions/IOException.h>

#define let const auto
namespace Jde{
	constexpr auto _tags{ ELogTags::App };
	α Process::FreeLibrary( void* p )ι->void{
		::dlclose( p );
	}

	α Process::LoadLibrary( const fs::path& path )ε->void*{
		auto p = ::dlopen( path.c_str(), RTLD_LAZY );
		THROW_IFX( !p, IO::IOException(SRCE_CUR, path, ELogLevel::Error, "Can not load library - '{}'", dlerror()) );
		INFO( "[{}] Opened", path.string() );
		return p;
	}
	α Process::GetProcAddress( void* module, str procName )ε->void*{
		auto p = ::dlsym( module, procName.c_str() ); CHECK( p );
		return p;
	}
	α Process::Install( str /*serviceDescription*/, const vector<string>& /*args*/ )ε->void{
		THROW( "Not Implemented" );
	}
	α Process::UnPause()ι->void{
		::raise( SIGALRM );//handled by ExitHandler - interrupts ::pause(). SIGKILL is uncatchable & would kill the process outright.
	}
	α Process::Uninstall()ε->void{
		THROW( "Not Implemeented");
	}

	α Process::Executable()ι->fs::path{
		return fs::path{ program_invocation_name };
	}

	α Process::AddApplicationLog( ELogLevel level, str value )ι->void{ //called onterminate, needs to be static.
		auto osLevel = LOG_DEBUG;
		if( level==ELogLevel::Debug )
			osLevel = LOG_INFO;
		else if( level==ELogLevel::Information )
			osLevel = LOG_NOTICE;
		else if( level==ELogLevel::Warning )
			osLevel = LOG_WARNING;
		else if( level==ELogLevel::Error )
			osLevel = LOG_ERR;
		else if( level==ELogLevel::Critical )
			osLevel = LOG_CRIT;
		syslog( osLevel, "%s",  value.c_str() );
	}
	const string _companyName{ "Jde-Cpp" }; string _productName{ "productName" };
	α Process::CompanyName()ι->string{ return _companyName; }
	α Process::MemorySize()ι->size_t{//https://stackoverflow.com/questions/669438/how-to-get-memory-usage-at-runtime-using-c
		uint size = 0;
		FILE* fp = fopen( "/proc/self/statm", "r" );
		if( fp!=nullptr ){
			long rss = 0L;
			if( fscanf( fp, "%*s%ld", &rss ) == 1 )
				size = (size_t)rss * (size_t)sysconf( _SC_PAGESIZE);
			fclose( fp );
		}
		return size;
	}

	α Process::ExePath()ι->fs::path{ return fs::canonical( "/proc/self/exe" ); }

	α Process::HostName()ι->string{
		char hostname[HOST_NAME_MAX+1]{};//+1 & zero-init: gethostname may not nul-terminate on truncation.
		if( ::gethostname(hostname, HOST_NAME_MAX)!=0 )
			ERR( "gethostname failed: {}", strerror(errno) );
		return hostname;
	}

	α Process::ProcessId()ι->uint{ return getpid(); }

	α Process::Pause()ι->int{
		INFOT( ELogTags::App, "Pausing main thread." );
		let paused = ::pause(); //-1/EINTR by contract - the signal, recorded by ExitHandler, is the result.
		let signal = ExitReason().value_or( -1 );
		INFOT( ELogTags::App, "Pause returned = {} (signal {}).", paused, signal );
		//A requested stop - SIGTERM (systemctl stop, kill) or ^C - is a clean exit; pause's -1 (exit code 255) had systemd log
		//every stop as a failure.  Anything else (SIGALRM from UnPause/Kill, SIGUSR1) keeps pause's result as before.
		let exitCode = signal==SIGTERM || signal==SIGINT ? EXIT_SUCCESS : paused;
		Shutdown( exitCode );
		return exitCode;
	}

	α Process::AsService()ι->bool{
		return ::daemon( 1, 0 )==0;
	}

	α Process::OnTerminate()ι->void{
		void *trace_elems[20];
		auto trace_elem_count( backtrace(trace_elems, 20) );
		char **stack_syms( backtrace_symbols(trace_elems, trace_elem_count) );
		std::ostringstream os;
		for( auto i = 0; i < trace_elem_count ; ++i )
			os << stack_syms[i] << std::endl;

		Process::AddApplicationLog( ELogLevel::Critical, os.str() );
		free( stack_syms );
		exit( EXIT_FAILURE );
	}

	α Process::ProgramDataFolder()ι->fs::path{
		//A system service:  a unit with StateDirectory=Jde-Cpp (apps/OpcHub/setup/linux) exports STATE_DIRECTORY=/var/lib/Jde-Cpp,
		//and the data root is its parent - paths-common.libsonnet and CompanyRootDir() append the company dir themselves.
		//Colon-separated when a unit names several directories; the first is ours.
		if( let state = GetEnv("STATE_DIRECTORY"); state )
			return fs::path{ state->substr(0, state->find(':')) }.parent_path();
		//Otherwise per-user - a console run, or a systemd --user unit:  $XDG_CONFIG_HOME, else $HOME/.config.
		return GetEnv("XDG_CONFIG_HOME").value_or( Process::GetEnv("HOME").value_or("/etc/app")+"/.config" );
	}

	α Process::ExitHandler( int s )->void{
		//std::cout << "Caught signal " << s << std::endl;
		if( !Process::ExitReason() )
			Process::SetExitReason( s, s==SIGTERM );
		//Handled in main.cpp
		//ASSERT( false ); //TODO handle
	//	signal( s, SIG_IGN );
	//not supposed to log here...
		//printf( "!!!!!!!!!!!!!!!!!!!!!Caught signal %d!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n",s );
		//pProcessManager->Stop();
		//delete pLogger; pLogger = nullptr;
		//exit( 1 );
	}

	α Process::Kill( uint processId )ι->bool{
		let result = ::kill( processId, 14 ); //SIGALRM
		if( result ){
			ERR( "kill failed with '{}'.", result );
		}else{
			INFO( "kill sent to:  '{}'.", processId );
		}
		return result==0;
	}

	up<flat_multimap<string,string>> _args;
	α Process::Args()ι->const flat_multimap<string,string>&{
		if( !_args ){
			_args = mu<flat_multimap<string,string>>();
			std::ifstream file( "/proc/self/cmdline" );
			vector<string> tokens;
			for( string current; std::getline<char>(file, current, '\0'); )
				tokens.push_back( move(current) );
			*_args = Process::ParseArgs( tokens );//the rules live in process.cpp - they were hand-rolled here and in WindowsApp, and had to agree.
		}
		return *_args;
	}

	α Process::CompanyRootDir()ι->fs::path{ return Process::CompanyName(); };

	α Process::AddSignals()ε->void{/*ε for windows*/
/* 		struct sigaction sigIntHandler;//_XOPEN_SOURCE
		memset( &sigIntHandler, 0, sizeof(sigIntHandler) );
		sigIntHandler.sa_handler = ExitHandler;
		sigemptyset( &sigIntHandler.sa_mask );
		sigIntHandler.sa_flags = 0;*/
		::signal( SIGINT, Process::ExitHandler );
		::signal( SIGTERM, Process::ExitHandler );
		::signal( SIGALRM, Process::ExitHandler );
		::signal( SIGUSR1, Process::ExitHandler );
		//sigaction( SIGSTOP, &sigIntHandler, nullptr );
		//sigaction( SIGKILL, &sigIntHandler, nullptr );
		//sigaction( SIGTERM, &sigIntHandler, nullptr );

/*		struct sigaction sa;
		memset( &sa, 0, sizeof(sa) );
	  sa.sa_flags = SA_RESTART | SA_SIGINFO;
		sa.sa_sigaction = IO::AioCompletionHandler;
		sigemptyset( &sa.sa_mask );
		THROW_IF( ::sigaction(IO::CompletionSignal, &sa, nullptr)==-1,  "init AsyncIO sigaction({}) returned {}", IO::CompletionSignal, errno );
*/
	}

	α Process::IsTerminal()ι->bool{ return ::isatty(STDOUT_FILENO)!=0; }
	α Process::PrepareConsole()ι->bool{ return true; }//a terminal draws them, and is UTF-8
	α Process::SetConsoleTitle( sv title )ι->void{
		if( IsTerminal() ) //a service runs with -c too (Type=simple), stdout on the journal - the escape would be its first line.
			std::cout << "\033]0;" << title << "\007";
	}

	// https://stackoverflow.com/questions/3596781/how-to-detect-if-the-current-process-is-being-run-by-gdb
	α Process::IsDebuggerPresent()ι->bool{
		char buf[4096];

    const int status_fd = open("/proc/self/status", O_RDONLY);
    if (status_fd == -1)
			return false;

    const ssize_t num_read = read( status_fd, buf, sizeof(buf) - 1 );
    close( status_fd );

    if( num_read <= 0 )
			return false;

    buf[num_read] = '\0';
    constexpr char tracerPidString[] = "TracerPid:";
    let tracer_pid_ptr = strstr( buf, tracerPidString );
    if( !tracer_pid_ptr )
			return false;

    for( const char* characterPtr = tracer_pid_ptr + sizeof(tracerPidString) - 1; characterPtr <= buf + num_read; ++characterPtr ){
			if (isspace(*characterPtr))
				continue;
			else
				return isdigit(*characterPtr) != 0 && *characterPtr != '0';
    }
    return false;
	}
}
#include <jde/fwk/process/process.h>
#include <iostream> // !important
#include <dlfcn.h>
#include <execinfo.h>
#include <semaphore.h>
#include <syslog.h>
#include <unistd.h>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/FileAwait.h>

#define let const auto
namespace Jde{
	constexpr auto _tags{ ELogTags::App };
	α Process::FreeLibrary( void* p )ι->void{
		::dlclose( p );
		Logging::ForgetSourceIds();
	}

	α Process::LoadLibrary( const fs::path& path )ε->void*{
		auto p = ::dlopen( path.c_str(), RTLD_LAZY );
		THROW_IFX( !p, IO::IOException(SRCE_CUR, path, ELogLevel::Error, "Can not load library - '{}'", dlerror()) );
		INFO( "[{}] Opened", path.string() );
		return p;
	}
	α Process::GetProcAddress( void* module, str procName )ε->void*{
		auto p = ::dlsym( module, procName.c_str() ); THROW_IF( !p, "dlsym('{}'): {}", procName, dlerror() );
		return p;
	}
	α Process::Install( str /*serviceDescription*/, const vector<string>& /*args*/ )ε->void{
		THROW( "Not Implemented" );
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
	const string _companyName{ "Jde-Cpp" };
	α Process::CompanyName()ι->string{ return _companyName; }
	α Process::MemorySize()ι->uint{//https://stackoverflow.com/questions/669438/how-to-get-memory-usage-at-runtime-using-c
		uint size = 0;
		FILE* fp = fopen( "/proc/self/statm", "r" );
		if( fp!=nullptr ){
			long rss = 0L;
			if( fscanf( fp, "%*s%ld", &rss ) == 1 )
				size = (uint)rss * (uint)sysconf( _SC_PAGESIZE);
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

	static sem_t _stopSignals;//counts:  a signal from before Pause - anywhere in the startup - is still here when it waits.
	constinit static atomic<int> _lastSignal{};//for the log:  the exit reason is an exit code.

	α Process::Pause()ι->int{
		INFOT( ELogTags::App, "Pausing main thread." );
		while( ::sem_wait(&_stopSignals)==-1 && errno==EINTR )
			continue;
		let exitCode = ExitReason().value_or( -1 );
		INFOT( ELogTags::App, "Pause ended on signal {}, exit code {}.", _lastSignal.load(), exitCode );
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

	Ω exitHandler( int s )->void{
		let savedErrno = errno;//the interrupted code's.
		_lastSignal = s;
		//A requested stop - SIGTERM (systemctl stop, kill) or ^C - is a clean exit; a -1 (exit code 255) had systemd log every
		//stop as a failure.  Anything else (SIGALRM from Kill, SIGUSR1) stays -1.
		Process::SetExitReason( s==SIGTERM || s==SIGINT ? EXIT_SUCCESS : -1, s==SIGTERM );
		::sem_post( &_stopSignals );//async-signal-safe, which absl::Notification and its kin are not.
		errno = savedErrno;
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
		::sem_init( &_stopSignals, 0, 0 );//before the handlers that post it.
		::signal( SIGINT, exitHandler );
		::signal( SIGTERM, exitHandler );
		::signal( SIGALRM, exitHandler );
		::signal( SIGUSR1, exitHandler );
	}

	α Process::IsTerminal()ι->bool{ return ::isatty(STDOUT_FILENO)!=0; }
	α Process::PrepareConsole()ι->bool{ return true; }//a terminal draws them, and is UTF-8
	α Process::RaiseTimerResolution()ι->void{}//linux honors a 1 ms wait.
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
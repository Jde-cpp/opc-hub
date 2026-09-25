#include <jde/fwk/process/process.h>
#include <iostream>
#include <Psapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <io.h> //_isatty
//#include "WindowsDrive.h"
#include "WindowsSvc.h"
#include "WindowsWorker.h"
#include "WindowsUtilities.h"

#define var const auto

namespace Jde{
	α Process::Kill( uint processId )ι->bool{
		INFOT( ELogTags::App | ELogTags::Shutdown, "Kill received - stopping instance" );
		var proc = ::OpenProcess( PROCESS_TERMINATE, false, (DWORD)processId );
		if( proc ){
			::TerminateProcess( proc, 1 );
			::CloseHandle( proc );
		}
		return proc;
	}
#undef SetConsoleTitle
	α Process::SetConsoleTitle( sv title )ι->void{
		::SetConsoleTitleA( Jde::format("{}({})", title, ProcessId()).c_str() );
	}
	α Process::IsTerminal()ι->bool{ return ::_isatty(::_fileno(stdout))!=0; }
	//A classic console window (conhost) - an elevated launch, Windows 10, a default terminal set to it - prints an escape sequence
	//as text until asked not to:  every line of an installed product's window was led by `←]8;;file:///…` (reviews/install-issues.md
	//#49).  Windows Terminal draws them either way.  And the console writes in its own code page (the OEM one, 437), not the
	//process's UTF-8 (build/utf8.manifest, #45) - it is the console's, so a cmd window the product ran in keeps it after.
	α Process::PrepareConsole()ι->bool{
		HANDLE out = ::GetStdHandle( STD_OUTPUT_HANDLE );
		DWORD mode{};
		if( out==INVALID_HANDLE_VALUE || !::GetConsoleMode(out, &mode) )
			return false;
		::SetConsoleOutputCP( CP_UTF8 );
		return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) || ::SetConsoleMode( out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING );
	}

	Ω handlerRoutine( DWORD ctrlType )->BOOL{
		bool handled{ true };
		var tags = ELogTags::App | ELogTags::Shutdown;
		switch( ctrlType ){
			case CTRL_C_EVENT: INFOT( tags, "Ctrl-C event" ); break;
			case CTRL_CLOSE_EVENT: INFOT( tags, "Ctrl-Close event" ); break;
			case CTRL_BREAK_EVENT: INFOT( tags, "Ctrl-Break event" ); break;
			case CTRL_LOGOFF_EVENT: INFOT( tags, "Ctrl-Logoff event" ); break;
			case CTRL_SHUTDOWN_EVENT: INFOT( tags, "Ctrl-Shutdown event" ); break;
			default: INFOT( tags, "Ctrl-C event unhanded: {:x}", ctrlType ); handled = false;
    }
		if( handled ) //was in WindowsWorker.cpp
			Windows::WindowsWorkerMain::Stop( ctrlType );
		return handled;
	}
	Ω stopRequested( PVOID, BOOLEAN )->void{
		INFOT( ELogTags::App | ELogTags::Shutdown, "Stop event" );
		Windows::WindowsWorkerMain::Stop( EXIT_SUCCESS );
	}
	//A console copy's stop, whatever hosts it (reviews/install-issues.md #42):  a polite taskkill is a close to the console
	//window, and Windows 11 hands an unelevated console launch to Windows Terminal, which leaves this process no window to
	//close - so Setup's reinstall always reached /F.  OpcHubSetup.nsi's CloseUserProduct signals this name.  Local\ - this
	//session's, as Setup's USERNAME filter is this user's.
	Ω addStopEvent()ι->void{
		var name = Jde::format( "Local\\{}.Stop", Process::AppName() );
		HANDLE stop = ::CreateEventA( nullptr, TRUE, FALSE, name.c_str() );
		HANDLE wait{};
		if( stop && ::RegisterWaitForSingleObject(&wait, stop, stopRequested, nullptr, INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION) )
			return;
		var error = ::GetLastError();
		WARNT( ELogTags::App | ELogTags::Startup, "Could not wait on '{}' - error {}.  Setup cannot close this copy in order; it will end it.", name, error );
		if( stop )
			::CloseHandle( stop );//so Setup finds no event and falls back to taskkill, rather than signal one nothing waits on.
	}
	bool _isService{false};
	α Process::AddSignals()ε->void{
		THROW_IF( !SetConsoleCtrlHandler(handlerRoutine, TRUE), "Could not set control handler" );
		if( !_isService )//a service's stop is the SCM's.
			addStopEvent();
	}

	α Process::MemorySize()ι->size_t{
		PROCESS_MEMORY_COUNTERS memCounter;
		::GetProcessMemoryInfo( ::GetCurrentProcess(), &memCounter, sizeof(memCounter) );
		return memCounter.WorkingSetSize;
	}
	α Process::ExePath()ι->fs::path{
		char* pgmptr = nullptr;
		_get_pgmptr( &pgmptr );
		return fs::path( pgmptr ? pgmptr : "" );
	}
	α Process::HostName()ι->string{
		DWORD maxHostName = 1024;
		char hostname[1024];
		if( !::GetComputerNameA(hostname, &maxHostName) )
			return "GetComputerNameA failed";

		return hostname;
	}
	α Process::ProcessId()ι->uint32{
		return _getpid();
	}

	α Process::OnTerminate()ι->void{
		//TODO Implement
	}

	//The windows half of linux's syslog write (LinuxApp.cpp).  Called from onterminate and the shutdown watchdog - after the
	//loggers are the likeliest thing wedged - so it must stay static, allocate as little as possible and never throw.
	α Process::AddApplicationLog( ELogLevel level, str value )ι->void{
		var type = level==ELogLevel::Critical || level==ELogLevel::Error
			? EVENTLOG_ERROR_TYPE
			: level==ELogLevel::Warning ? EVENTLOG_WARNING_TYPE : EVENTLOG_INFORMATION_TYPE;
		HANDLE source = ::RegisterEventSource( nullptr, string{Process::AppName()}.c_str() );
		if( !source )
			return;//nothing to fall back on:  the log is what is broken.
		const char* strings[]{ value.c_str() };
		::ReportEvent( source, (WORD)type, 0, 0, nullptr, 1, 0, strings, nullptr );
		::DeregisterEventSource( source );
	}

	α Process::AsService()ι->bool{
		_isService = true;
		Windows::Service::ReportStatus( SERVICE_START_PENDING, NO_ERROR, 3000 );
		return true;
	}

	up<flat_multimap<string,string>> _args;
	α Process::Args()ι->const flat_multimap<string,string>&{
		if( !_args ){
			_args = mu<flat_multimap<string,string>>();
			int nArgs;
			LPWSTR* szArglist = ::CommandLineToArgvW( ::GetCommandLineW(), &nArgs );
			if( !szArglist )
				std::cerr << "CommandLineToArgvW failed\n";
		   else{
				vector<string> tokens; tokens.reserve( (uint)nArgs );
				for( int i=0; i<nArgs; ++i )
					tokens.push_back( Windows::ToString(szArglist[i]) );
				*_args = Process::ParseArgs( tokens );//the rules live in process.cpp - they were hand-rolled here and in LinuxApp, and had to agree.
			  LocalFree(szArglist);
			}
		}
		return *_args;
	}
	α Process::IsDebuggerPresent()ι->bool{
		return ::IsDebuggerPresent() != 0;
	}

	α Process::Executable()ι->fs::path{
		return fs::path{ Windows::ToWString(Process::Args().find( {} )->second) };//the args are UTF-8 (Windows::ToString above) - fs::path's narrow constructor would read them in the ANSI code page (reviews/m4-closing.md #3)
	}

	α Process::UnPause()ι->void{
		Windows::WindowsWorkerMain::Stop( 0 );
	}

	α Process::Pause()ι->int{
		INFOT( ELogTags::App | ELogTags::Startup, "Starting main thread loop...{}", _getpid() );
		if( _isService ){
			SERVICE_TABLE_ENTRY DispatchTable[] = {  { (char*)Process::AppName().data(), (LPSERVICE_MAIN_FUNCTION)Windows::Service::Main },  { nullptr, nullptr }  };
			var success = StartServiceCtrlDispatcher( DispatchTable );//blocks?
			if( !success )
				Windows::Service::ReportEvent( "StartServiceCtrlDispatcher" );
		}
		else
			Windows::WindowsWorkerMain::Start( false );
		return 1;
	}
	string _companyName;

//could get run before initialize logger.
#define CHECK_NOLOG(condition) if( !(condition) ) throw Jde::Exception{ SRCE_CUR, Jde::ELogLevel::NoLog, "error: {}", #condition }
	Ω loadResource( sv key )ι->string{//the running module's own version resource, by its wide path through the W calls (A and W must not mix on one block).  It took argv[0] - UTF-8 by then - through the A calls, which read it in the ANSI code page:  under C:\Users\Zoë that found no resource, and ProductName() forked the data and certificate tree as "Jde-cpp" (reviews/m4-closing.md #3)
		string y;
		try{
			std::wstring exe( 32767, L'\0' );//the longest path there is - GetModuleFileNameW cannot truncate
			exe.resize( ::GetModuleFileNameW(nullptr, exe.data(), (DWORD)exe.size()) );
			CHECK_NOLOG( exe.size() );
			DWORD _;
			var size = ::GetFileVersionInfoSizeW( exe.c_str(), &_ );
			if( !size )
				return y;
			vector<BYTE> block( size );
			CHECK_NOLOG( ::GetFileVersionInfoW(exe.c_str(), 0, size, block.data()) );
			struct LANGANDCODEPAGE { WORD wLanguage; WORD wCodePage; } *lpTranslate; UINT cbTranslate;
			CHECK_NOLOG( ::VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate) && cbTranslate>=sizeof(LANGANDCODEPAGE) );
			var name = Windows::ToWString( Jde::format("\\StringFileInfo\\{:04x}{:04x}\\{}", lpTranslate[0].wLanguage, lpTranslate[0].wCodePage, key) ); wchar_t* value; UINT chars;
			CHECK_NOLOG( ::VerQueryValueW(block.data(), name.c_str(), (LPVOID*)&value, &chars) && chars );
			y = Windows::ToString( std::wstring{value, ::wcsnlen(value, chars)} );//chars counts the terminator - or not, by resource compiler
		}
		catch( const runtime_error& )
		{}
		return y;
	}

	α Process::CompanyName()ι->string{
		if(! _companyName.size() ){
			_companyName = loadResource( "CompanyName" );
			if( _companyName.empty() )
				_companyName = "Jde-Cpp";//matches LinuxApp's literal and every .rc - this is CompanyRootDir, so a second spelling forks the data dir on a case-sensitive filesystem.
		}
		return _companyName;
	}
	string _productName;
	α Process::ProductName()ι->sv{
		if( _productName.empty() ){
			_productName = loadResource( "ProductName" );
			if( _productName.empty() )//said, not just done:  this name is the data and certificate dir, and a silent one forked the tree with nothing to name the cause (#3).  stderr - perhaps before the logger, and the console mode is where that happened; not the event log - the test exes carry no .rc, so this is their name on every run.
				std::cerr << "No ProductName in the exe's version resource - the product dir is '" << (_productName = "Jde-cpp") << "', which no installed setting names.\n";
		}
		return _productName;
	}
	//α Process::SetProductName( sv productName )ι->void{
	//	if( Process::ProductName() == "Jde-cpp" )
	//		_productName = productName;
	//}
	α Process::CompanyRootDir()ι->fs::path{ return Process::CompanyName(); }

/*	α IApplication::EnvironmentVariable( str variable, SL sl )ι->optional<string>{
		char buffer[32767];
		optional<string> result;
		if( !::GetEnvironmentVariable(variable.c_str(), buffer, sizeof(buffer)) )
			Logging::LogOnce( sl, ELogTags::Settings, "GetEnvironmentVariable('{}') failed:  {}", variable,  Ƒ("{:x}", ::GetLastError()) );
		else
			result = buffer;

		return result;
	}*/
	α Process::ProgramDataFolder()ι->fs::path{
		return fs::path{ GetEnv("ProgramData").value_or("") };
	}

	struct SCDeleter
	{
		void operator()(SC_HANDLE p){ if( p ) ::CloseServiceHandle(p); }
	};
	using ServiceHandle = std::unique_ptr<SC_HANDLE__, SCDeleter>;

	ServiceHandle MyOpenSCManager()ε{
		auto schSCManager = ServiceHandle{ ::OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS) };
		if( !schSCManager.get() ){
			if( ::GetLastError() == ERROR_ACCESS_DENIED )
				THROW( "installation requires administrative privliges." );
			else
				THROW( "OpenSCManager failed - {}", ::GetLastError() );
		}
		return schSCManager;
	}

	α Process::Install( str serviceDescription, const vector<string>& args )ε->void{
		auto schSCManager = MyOpenSCManager();
		const string serviceName{ Process::AppName() };
		//The SCM launches this line verbatim, so it carries the caller's -settings/-include/-sync and quotes the exe - see ServiceCommandLine.
		const auto commandLine = ServiceCommandLine( ExePath(), args );
		auto service = ServiceHandle{ ::CreateService(schSCManager.get(), serviceName.c_str(), (serviceName).c_str(), SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, commandLine.c_str(), nullptr, nullptr, nullptr, "NT AUTHORITY\\LocalService", nullptr) };//Local Service, not the SCM's default LocalSystem:  nothing the service does needs SYSTEM, and the .deb runs it as an unprivileged account - one identity for every product, so the hub can still stop the OpcServer (AppInstanceHook's Process::Kill); the installer grants it its data dirs (reviews/m4-closing.md #10)
		if( !service.get() ){
			if( ::GetLastError()==ERROR_SERVICE_EXISTS )
				THROW( "Service already exists." );
			else
				THROW( "CreateService failed - {}", ::GetLastError() );
		}
		if( serviceDescription.size() ){
			SERVICE_DESCRIPTION d{ (LPSTR)serviceDescription.c_str() };
			if( !::ChangeServiceConfig2A(service.get(), SERVICE_CONFIG_DESCRIPTION, &d) )
				std::cerr << "ChangeServiceConfig2A failed" << std::endl;
		}
		INFOT( ELogTags::App, "service '{}' installed successfully:  {}", serviceName, commandLine );
	}
	α Process::Uninstall()ε->void{
		auto manager = MyOpenSCManager();
		auto service = ServiceHandle{ ::OpenService(manager.get(), Process::AppName().c_str(), DELETE) };
		if( !service.get() ){
			if( ::GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST )
				THROW( "DeleteService failed - {}", ::GetLastError() );
			INFOT( ELogTags::App, "Service '{}' not found - nothing to uninstall.", Process::AppName() );//already gone is what -uninstall asks for:  a reinstall after an aborted Setup, or an `sc delete`, meets it, and a throw here reached the Application log as Critical (reviews/m4-closing.md #10)
			return;
		}
		THROW_IF( !::DeleteService(service.get()), "DeleteService failed:  {:x}", GetLastError() );

		INFOT( ELogTags::App, "Service '{}' deleted successfully", Process::AppName() );
	}
#undef LoadLibrary
	α Process::LoadLibrary( const fs::path& path )ε->void*{
		//LOAD_WITH_ALTERED_SEARCH_PATH resolves the module's own imports from *its* directory rather than the exe's.  The
		//drivers are loaded out of bin/ beside their dependencies (sqlite3.dll, libcrypto-3-x64.dll), while the exe doing
		//the loading may live in bin/<target>/ - the default order searches next to the exe, so a driver that is present
		//still fails to load with 126.  The flag requires a fully-qualified native path, hence the normalize.
		var native = path.lexically_normal().make_preferred().string();
		auto p = ::LoadLibraryExA( native.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH ); THROW_IFX( !p, IO::IOException(path, GetLastError(), "Can not load library") );
		INFOT( ELogTags::App, "({})Opened"sv, path.string() );
		return p;
	}

	α Process::FreeLibrary( void* p )ι->void{
		::FreeLibrary( (HMODULE)p );
	}
#pragma clang diagnostic ignored "-Wmicrosoft-cast"
	α Process::GetProcAddress( void* pModule, str procName )ε->void*{
		auto p = ::GetProcAddress( (HMODULE)pModule, procName.c_str() ); CHECK( p );
		return p;
	}
}
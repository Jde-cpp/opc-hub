#include "WindowsSvc.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <iostream>
#include "WindowsWorker.h"

#define var const auto
constexpr DWORD SVC_ERROR=0xC0020001L;

namespace Jde::Windows{
	constexpr ELogTags _tags{ ELogTags::Settings };
	SERVICE_STATUS _svcStatus{};
	SERVICE_STATUS_HANDLE _svcStatusHandle{}; //under _statusMutex
	DWORD _dwCheckPoint{0};
	mutex _statusMutex; //ReportStatus runs on the SCM's thread, Main's (while pending, then the worker's loop) and the main thread's shutdown

	//The SCM connection used to come last:  StartServiceCtrlDispatcher in Process::Pause, after the whole startup.  A startup
	//that failed - a database that was not up, a port another copy held - exited before it, so the SCM saw a service that never
	//connected (7000/7009) and applied none of its failure actions:  the README's `sc failure` restart never fired
	//(reviews/install-issues.md #68).  Now the dispatcher runs first, on its own thread, and Main reports start pending until
	//the startup ends:  in running (Started), or in stopped with the startup's exit code (a shutdown function, below) - which
	//`sc failureflag` makes a failure.
	HandlePtr _connected; //Main has its status handle, or the dispatcher could not start
	HandlePtr _started;   //the startup finished, well or not
	std::atomic<bool> _failed{}; //set by the main thread's shutdown, read by Main on the dispatcher's
	std::thread _dispatcher;
	std::atomic<bool> _joining{}; //Started owns the join from here:  a stop's shutdown must not detach under it

	Ω WINAPI svcCtrlHandler( DWORD dwCtrl )->void{
		switch(dwCtrl){
		case SERVICE_CONTROL_STOP:
			Service::ReportStatus( SERVICE_STOP_PENDING, NO_ERROR, 0 );
			WindowsWorkerMain::Stop( SERVICE_CONTROL_STOP ); //Loop reports stopped once it returns
			return;
		case SERVICE_CONTROL_INTERROGATE:
			break;
		default:
			break;
		}
	}

	//Under _statusMutex.  Stopped is final:  the SCM has retired the handle, and a later report - Main's pending wait, that saw pending just
	//before a failed startup's stop - would regress the state it holds.
	Ω report( DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint )ι->void{
		if( _svcStatus.dwCurrentState==SERVICE_STOPPED )
			return;
		_svcStatus.dwCurrentState = dwCurrentState;
		_svcStatus.dwWin32ExitCode = dwWin32ExitCode;
		_svcStatus.dwWaitHint = dwWaitHint;
		_svcStatus.dwControlsAccepted = dwCurrentState == SERVICE_START_PENDING ? 0 : SERVICE_ACCEPT_STOP;
		_svcStatus.dwCheckPoint = dwCurrentState == SERVICE_RUNNING || dwCurrentState == SERVICE_STOPPED ? 0 : ++_dwCheckPoint;
		if( _svcStatusHandle ) //none when not started by the SCM - or not yet
			SetServiceStatus( _svcStatusHandle, &_svcStatus );
	}
	α Service::ReportStatus( DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint )ι->void{
		lg _{ _statusMutex };
		report( dwCurrentState, dwWin32ExitCode, dwWaitHint );
	}

	α Service::ReportEvent( sv function )ι->void{
		string buffer = Jde::format( "{} failed with {}", function, GetLastError() );
		HANDLE hEventSource = ::RegisterEventSource( nullptr, string{Process::AppName()}.c_str() );
		if( !hEventSource ){
			CRITICALT( _tags, "RegisterEventSource returned null" );
			return;
		}
		const char* lpszStrings[2] = { Process::AppName().data(), buffer.data() };
		::ReportEvent( hEventSource, EVENTLOG_ERROR_TYPE, 0, SVC_ERROR, nullptr, 2, 0, lpszStrings, nullptr );
		DeregisterEventSource( hEventSource );
	}

	Ω pending()ι->bool{ lg _{ _statusMutex }; return _svcStatus.dwCurrentState==SERVICE_START_PENDING; }
	Ω reportIfPending( DWORD waitHint )ι->void{
		lg _{ _statusMutex };
		if( _svcStatus.dwCurrentState==SERVICE_START_PENDING )
			report( SERVICE_START_PENDING, NO_ERROR, waitHint );
	}

	//Start pending accepts no controls, and Main's pending wait keeps the SCM from timing it out:  a startup that blocks - a database
	//host that drops packets - would be a service nothing but taskkill could end.  Past the bound, exit hard, as the shutdown
	//watchdog does:  the SCM logs an unexpected termination and runs the failure actions (reviews/win-service.md #3).
	Duration _startupTimeout;
	Ω startupTimeout()ι->Duration{
		if( const auto configured = Settings::FindDuration("/startup/timeout"); configured )
			return *configured;
		return Process::IsDebuggerPresent() ? Duration{30min} : Duration{5min};
	}
	[[noreturn]] Ω startupTimedOut()ι->void{
		const auto message = Ƒ( "Startup did not complete within {}s - exiting.", duration_cast<std::chrono::seconds>(_startupTimeout).count() );
		std::cerr << message << std::endl;
		Process::AddApplicationLog( ELogLevel::Critical, message );
		LOG( ELogLevel::Critical, ELogTags::App | ELogTags::Startup, "{}", message );
		std::_Exit( ERROR_SERVICE_REQUEST_TIMEOUT );
	}

	α Service::Main( DWORD /*dwArgc*/, char** /*lpszArgv*/ )ι->void{
		const auto handle = RegisterServiceCtrlHandler( string{Process::AppName()}.c_str(),  svcCtrlHandler );
		{ lg _{ _statusMutex }; _svcStatusHandle = handle; }
		if( !handle ){
			Service::ReportEvent( "RegisterServiceCtrlHandler" );
			::SetEvent( _connected.get() );
			return;
		}
		_svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
		_svcStatus.dwServiceSpecificExitCode = 0;
		constexpr DWORD waitHint{ 10'000 };
		ReportStatus( SERVICE_START_PENDING, NO_ERROR, waitHint );
		::SetEvent( _connected.get() );
		const auto deadline = steady_clock::now()+_startupTimeout;
		while( ::WaitForSingleObject(_started.get(), 2'000)==WAIT_TIMEOUT ){//a schema sync can outlast a wait hint:  a checkpoint that moves says the start is alive
			if( _startupTimeout>Duration::zero() && steady_clock::now()>deadline && !Process::ShuttingDown() && pending() )//a failed startup's cleanup is the shutdown watchdog's
				startupTimedOut();
			reportIfPending( waitHint );
		}
		if( !_failed )
			WindowsWorkerMain::Start( true );
	}

	α Service::Connect()ι->bool{
		_startupTimeout = startupTimeout(); //before the dispatcher's thread, which reads it
		_connected = ManualResetEvent();
		_started = ManualResetEvent();
		_dispatcher = std::thread{ []{
			SERVICE_TABLE_ENTRY dispatchTable[] = { { (char*)Process::AppName().data(), (LPSERVICE_MAIN_FUNCTION)Service::Main }, { nullptr, nullptr } };
			if( !::StartServiceCtrlDispatcher(dispatchTable) ){
				Service::ReportEvent( "StartServiceCtrlDispatcher" );
				::SetEvent( _connected.get() ); //not started by the SCM:  nothing to wait for
			}
		}};
		const bool connected = ::WaitForSingleObject( _connected.get(), 30'000 )==WAIT_OBJECT_0; //the SCM's own limit for a service to connect
		//A startup that throws ends in Process::Shutdown, not in Started:  report it stopped, with its exit code, while the
		//start is still pending - a stop the SCM counts as a failure (`sc failureflag`), where an exit it never saw is not.
		Process::AddShutdownFunction( []( bool, SL ){
			lg _{ _statusMutex };
			if( _svcStatus.dwCurrentState==SERVICE_START_PENDING ){
				_failed = true;
				_svcStatus.dwServiceSpecificExitCode = (DWORD)Process::ExitReason().value_or( EXIT_FAILURE );
			}
		});
		//Stopped only once the ports, the database and the log are let go:  a restart action starts the next copy on it.
		Process::AddExitFunction( []{
			if( _failed ){
				ReportStatus( SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0 );
				::SetEvent( _started.get() );
			}
			if( !_joining && _dispatcher.joinable() ){ //a startup that failed before Started - under the SCM, or run without it
				if( ::WaitForSingleObject(_dispatcher.native_handle(), 5'000)==WAIT_OBJECT_0 ) //returns once the SCM has the stop
					_dispatcher.join();
				else
					_dispatcher.detach(); //one the SCM never let go:  a joinable thread at exit would terminate
			}
		});
		if( !connected )
			CRITICALT( _tags, "The service control manager did not connect within 30s." );
		return connected;//the functions above are registered either way:  the exit one owns the dispatcher's thread
	}

	α Service::Started()ι->void{
		_joining = true;
		::SetEvent( _started.get() );
		if( _dispatcher.joinable() )
			_dispatcher.join(); //Main runs the worker's loop;  the dispatcher returns when it stops
	}
}
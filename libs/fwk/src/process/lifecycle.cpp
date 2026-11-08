#include <jde/fwk/process/process.h>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <typeinfo>
#include <absl/synchronization/notification.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/utils/Vector.h>

#define let const auto

namespace Jde{
	enum class EExit : int{ None, Stop, Terminate };
	struct ExitCause{ int Code; EExit Kind; };//no padding:  compare_exchange compares the bytes.
	static_assert( atomic<ExitCause>::is_always_lock_free );//the signal handler records it.
	constinit static atomic<ExitCause> _cause{ ExitCause{} };

	enum class EPhase : uint8{ Running, Stopping, Draining, Finalizing, LoggersGone };//only ever forward.
	constinit static atomic<EPhase> _phase{ EPhase::Running };

	α Process::SetExitReason( int code, bool terminate )ι->void{
		ExitCause none{};
		_cause.compare_exchange_strong( none, ExitCause{code, terminate ? EExit::Terminate : EExit::Stop} );
	}
	α Process::ExitReason()ι->optional<int>{
		let cause = _cause.load();
		return cause.Kind==EExit::None ? optional<int>{} : cause.Code;
	}
	α Process::ShuttingDown()ι->bool{ return _cause.load().Kind!=EExit::None; }
	α Process::LoggersGone()ι->bool{ return _phase.load()==EPhase::LoggersGone; }

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
		SetExitReason( y, false );
		(y==0 ? std::cout : std::cerr) << prefix << message << std::endl;
		if( y!=EXIT_SUCCESS && !IsConsole() )//a service's stderr goes nowhere, and the SCM's record of a failed start (7024) carries only the exit code:  this event names the cause, and Setup's StartService shows it.
			AddApplicationLog( ELogLevel::Critical, Ƒ("{}{}", prefix, message) );
		return y;
	}

	static Vector<function<void(bool, SL)>> _shutdownFunctions;
	α Process::AddShutdownFunction( function<void(bool, SL)>&& shutdown )ι->void{
		_shutdownFunctions.push_back( move(shutdown) );
	}

	static Vector<function<void(bool)>> _finalizeFunctions;
	α Process::AddFinalizeFunction( function<void(bool)>&& finalize )ι->void{
		_finalizeFunctions.push_back( move(finalize) );
	}

	static Vector<absl::AnyInvocable<void() const>> _exitFunctions;
	α Process::AddExitFunction( absl::AnyInvocable<void() const>&& exit )ι->void{
		_exitFunctions.push_back( std::move(exit) );
	}

	static Vector<IShutdown*> _rawShutdowns;
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

	static absl::Notification _shutdownFinished;
	Ω shutdownTimeout()ι->Duration{
		if( let configured = Settings::FindDuration("/shutdown/timeout"); configured )
			return *configured;
		return Process::IsDebuggerPresent() ? Duration{5min} : Duration{30s};//stepping through teardown in a debugger is not a wedge - give it room, but still bound it.
	}
	//A process that runs its shutdown functions - closing its listeners - and then never returns is worse than
	//a hard exit: it keeps its AppServer registration and goes on advertising ports it has already closed.  Nothing stops new
	//work arriving mid-teardown (the 100ms sleep in Shutdown is not a barrier), so this is the backstop.  reviews/gateway-review.md #33.
	Ω armShutdownWatchdog( int exitCode )ι->void{
		let timeout = shutdownTimeout();
		if( timeout<=Duration::zero() )
			return;
		std::thread{ [timeout,exitCode](){
			Thread::SetName( "ShutdownWatchdog" );
			if( _shutdownFinished.WaitForNotificationWithTimeout(absl::FromChrono(timeout)) )
				return;
			let message = Ƒ( "Shutdown did not complete within {}ms - exiting hard.", std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count() );
			std::cerr << message << std::endl;//the loggers are the likeliest thing wedged - say it somewhere that cannot be.
			Process::AddApplicationLog( ELogLevel::Critical, message );
			std::_Exit( exitCode ? exitCode : EXIT_FAILURE );//not exit(): atexit handlers & static destructors are exactly what is stuck.  A wedge is never a success.
		} }.detach();
	}

	α Process::Shutdown( int exitCode )ι->void{
		auto running = EPhase::Running;
		if( !_phase.compare_exchange_strong(running, EPhase::Stopping) )//a windows stop runs this twice - WindowsWorkerMain::Stop, then main with Pause's result; the 2nd pass re-ran the shutdown functions against already-nulled state (e.g. AppServer's AccessListener).
			return;
		SetExitReason( exitCode, false );//the signal handler may have recorded the reason & terminate flag (SIGTERM) already - first cause wins.
		let cause = _cause.load();
		let terminate = cause.Kind==EExit::Terminate;
		armShutdownWatchdog( cause.Code );

		_shutdownFunctions.erase( [=]( const function<void(bool, SL)>& shutdown ){ shutdown(terminate, SRCE_CUR); } );
		DBGT( ELogTags::App | ELogTags::Shutdown, "Shutdown functions run" );
		_rawShutdowns.rerase( [=](auto& p){
			p->Shutdown( terminate );
		});
		DBGT( ELogTags::App | ELogTags::Shutdown, "Raw functions run" );
		if( terminate )//Execution::Stop stops the io_context at once:  this is all a fire-and-forget hook's continuation gets (UAClient::Shutdown).  Without terminate the stop drains, and waits for it.
			std::this_thread::sleep_for( 100ms );

		_phase = EPhase::Draining;
		Execution::Stop( terminate );//joins the executor's threads.
		_phase = EPhase::Finalizing;
		_finalizeFunctions.erase( [=]( const function<void(bool)>& finalize ){ finalize(terminate); } );
		_phase = EPhase::LoggersGone;//here, not before the drain:  what the drain logs is what a wedge needs.
		Logging::DestroyLoggers( terminate );
		auto ioc = Execution::Release();//destroyed last — after sessions, loggers and timers release their asio objects.
		if( ioc && ioc.use_count()>1 )//everything that used the io_context should have released it by now; a leftover ref means an asio object would otherwise outlive the io_context (use-after-free).
			std::cout << "WARNING: io_context still has " << ioc.use_count()-1 << " reference(s) at finalize." << std::endl;
		ioc = nullptr;//io_context destroyed here, deterministically last.
		_exitFunctions.erase( []( const absl::AnyInvocable<void() const>& exit ){ exit(); } );//still under the watchdog
		_shutdownFinished.Notify();//disarms the watchdog.  Once only:  Shutdown runs once.
		std::cout << "Shutdown complete." << std::endl;
	}
}
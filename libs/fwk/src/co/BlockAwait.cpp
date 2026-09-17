#include <boost/asio/io_context.hpp>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>

#define let const auto
namespace Jde{
	constexpr ELogTags _tags{ ELogTags::App | ELogTags::Threads };

	//How long a blocked caller waits before it starts saying so; 0 disables the warning.  The wait itself is never bounded
	//- see the note on BlockAwaitSync.
	Ω stallWarning()ι->Duration{
		static const Duration _interval = Settings::FindDuration( "/workers/blockStallWarning" ).value_or( Duration{30s} );
		return _interval;
	}

	α BlockAwaitSync::Signal()ι->void{
		{ lg _{_mutex}; _done = true; }
		_cv.notify_all();
	}

	//A BlockAwait that parks an executor thread is waiting for work that may need that very thread - a query co_spawned
	//onto the pool, a resume posted to it - and with a small pool the wedge is silent until the stall warning below, which
	//says "waiting", not "waiting for itself".  Named once per call site:  db-review3 #1 predicted it, emulator-review W1
	//watched it take the AppServer's registrations down.
	Ω warnIfOnExecutor( SL sl )ι->void{
		if( !Executor()->get_executor().running_in_this_thread() )
			return;
		static concurrent_flat_set<string> _sites;
		if( _sites.insert(Ƒ("{}:{}", sl.file_name(), sl.line())) )
			LOGSL( ELogLevel::Warning, sl, _tags, "BlockAwait entered on an executor thread - if its result needs this pool, that is a deadlock in waiting (db-review3 #1).  Awaiting it (Any()) frees the thread." );
	}
	α BlockAwaitSync::Wait( SL sl, ELogLevel stallLevel )ι->void{
		std::unique_lock l{ _mutex };
		if( !_done )
			warnIfOnExecutor( sl );
		let interval = stallWarning();
		if( interval<=Duration::zero() ){
			_cv.wait( l, [this](){return _done;} );
			return;
		}
		//Past the first interval this thread is parked on something that should already have answered.  It keeps waiting - the
		//caller's contract is a value, not a timeout - but says so every interval, and says where from: the whole cost of the
		//2026-08-07 gateway stall was that a dropped response looked exactly like a process quietly doing nothing.
		for( uint i=1; !_cv.wait_for(l, interval, [this](){return _done;}); ++i ){
			if( Process::Finalizing() )//the loggers are gone by then; a stall during finalize is the shutdown watchdog's problem.
				continue;
			//"{}" and a pre-formatted string, not "{:.1f}" and a double: Logging::Entry stringifies its arguments, so a spec
			//that only applies to a number makes Entry::Message() take its format-error path - which logs, from inside
			//MemoryLog::Find's write lock.
			//stallLevel (Await.h) caps the level for a wait the caller knows to be long; a real hang still climbs to Critical.
			LOGSL( std::min(i==1 ? ELogLevel::Warning : ELogLevel::Critical, stallLevel), sl, _tags, "BlockAwait has been waiting {}s for its result.", Ƒ("{:.1f}", std::chrono::duration<double>(interval*static_cast<Duration::rep>(i)).count()) );
		}
	}
}

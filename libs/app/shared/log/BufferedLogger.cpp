#include <jde/app/log/BufferedLogger.h>
#include <jde/fwk/process/execution.h>
#include <thread>

#define let const auto

namespace Jde::App{
	BufferedLogger::BufferedLogger( const jobject& settings, uint maxSize )ε:
		ILogger{ settings },
		_delay{ std::max<Duration>(Json::FindDuration(settings, "delay", ELogLevel::Error).value_or(1min), Duration::zero()) },//negative: the timer completes inline, under the caller's lock.
		_maxSize{ maxSize }{
		Executor();//locks up if started in a round.
		Execution::Run();
	}
	//Unbounded, with C10's caveat: a cancel completion posted to an io_context that has already stopped never runs, and then only
	//the watchdog ends this.
	α BufferedLogger::Stop()ι->void{
		StopTimer();//_delay=min() first, so the cancelled round's continuation arms nothing.
		while( _running )
			std::this_thread::sleep_for( 1ms );
	}
	α BufferedLogger::ArmTimer()ι->void{
		if( _timer || Stopping() )
			return;
		_timer = mu<DurationTimer>( _delay );
		Rounds( *_timer );
	}
	//M5/#14: the one bound on a buffer that grows for as long as nothing drains it.  Drop keeps the newest - the entries describing
	//whatever is going wrong.
	α BufferedLogger::Cap()ι->bool{
		if( Size()<=_maxSize )
			return false;
		let dropped = Drop();
		let first = dropped && !_dropped;
		_dropped += dropped;
		return first;
	}

	//A loop, not a tail call.  Restarting itself meant the replacement frame was live while the one that spawned it was still
	//finishing, so no single flag could say "nobody is running" - only a count could (#12).  _timer stays set for the whole round:
	//dropping it early is what let ArmTimer - which starts a round only when _timer is null - spawn a second frame alongside this one.
	//The first await runs under the caller's lock; every later one is armed under _mutex and awaited without it.  Not analyzed: a
	//per-function analysis cannot follow the lock across the awaits.
	ABSL_NO_THREAD_SAFETY_ANALYSIS α BufferedLogger::Rounds( DurationTimer& first )ι->TimerAwait::Task{
		Running _{ *this };//M9: this frame resumes into `this` - Stop waits for it.
		for( auto timer = &first;; ){
			let fired = co_await *timer;
			_mutex.lock();
			if( fired && !Stopping() && Size() ){
				Flush();//releases _mutex.
				_mutex.lock();
			}
			if( Stopping() || !Size() ){//Size also covers entries written during Flush.
				_timer.reset();
				_mutex.unlock();
				co_return;
			}
			timer = ( _timer = mu<DurationTimer>(_delay) ).get();
			_mutex.unlock();
		}
	}
	//Rounds resets _timer - destroying the DurationTimer - under this same lock, so an unguarded `if( _timer ) _timer->Cancel()` could
	//pass the test on the shutdown thread and then call Cancel() on freed memory once the io thread ran the continuation.
	//Safe to hold _mutex across Cancel(): it takes the DurationTimer's own lock and calls asio's cancel(), which *posts* the completion
	//- the continuation that re-takes _mutex never runs on this stack, so there is no re-entry to deadlock on, and the lock order is
	//only ever BufferedLogger -> DurationTimer.
	α BufferedLogger::ResetTimerUnlocked()ι->void{
		if( _timer )
			_timer->Cancel();
	}
	//One locked operation, because it is two writes: Rounds reads _delay under _mutex, so setting it from the shutdown thread without
	//the lock raced the very timer being cancelled.
	α BufferedLogger::StopTimer()ι->void{
		ul _{ _mutex };
		_delay = Duration::min();
		ResetTimerUnlocked();
	}
}
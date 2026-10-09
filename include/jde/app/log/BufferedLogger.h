#pragma once
#include <absl/synchronization/mutex.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/log/ILogger.h>

namespace Jde::App{
	//ProtoLog and RemoteLog: a buffer drained by a timer round, capped while nothing drains it, and a destructor that outlasts the
	//round.  The final class owns the buffer and says how to flush and trim it.
	struct BufferedLogger : Logging::ILogger, noncopyable{
		BufferedLogger( const jobject& settings, uint maxSize )ε;
	protected:
		//Held as a local by every frame that resumes into `this`, so the count falls when the frame is destroyed however it exits.
		//An atomic rather than a _mutex-guarded field: these frames drop and retake _mutex around their awaits, so a destructor that
		//took the lock could deadlock against one of them.
		struct Running final{
			Running( BufferedLogger& log )ι:_n{log._running}{ ++_n; }
			~Running(){ --_n; }
		private:
			atomic<uint>& _n;
		};
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α ArmTimer()ι->void;//starts a round unless one is running or StopTimer ran.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Cap()ι->bool;//true on the first drop since TakeDropped - the caller warns, outside _mutex.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Delay()Ι->Duration{ return _delay; }
		α MaxSize()Ι->uint{ return _maxSize; }
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α ResetTimerUnlocked()ι->void;
		ABSL_LOCKS_EXCLUDED(_mutex) α Stop()ι->void;//first thing in the final class's destructor: Flush and Size dispatch to it.
		ABSL_LOCKS_EXCLUDED(_mutex) α StopTimer()ι->void;//shutdown: no round starts again.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α TakeDropped()ι->uint{ return std::exchange( _dropped, 0u ); }//ends the outage Cap warns once for.

		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) β Drop()ι->uint=0;//trims the oldest half; returns what went, in Size's units.
		ABSL_UNLOCK_FUNCTION(_mutex) β Flush()ι->void=0;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) β Size()Ι->uint=0;

		mutable absl::Mutex _mutex;
		static constexpr ELogTags _tags{ ELogTags::ExternalLogger };
	private:
		α Rounds( DurationTimer& first )ι->TimerAwait::Task;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Stopping()Ι->bool{ return _delay==Duration::min(); }

		Duration _delay ABSL_GUARDED_BY(_mutex);
		uint _dropped ABSL_GUARDED_BY(_mutex){};
		const uint _maxSize;
		atomic<uint> _running{};
		up<DurationTimer> _timer ABSL_GUARDED_BY(_mutex);
	};
}
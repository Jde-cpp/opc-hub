#include <jde/fwk/io/Cache.h>
#include <absl/synchronization/mutex.h>
#include <chrono>
#include <thread>
#include "jde/fwk/log/logTags.h"
#include "jde/fwk/process/process.h"
#include "jde/fwk/usings.h"
#include <jde/fwk/co/Timer.h>

#define let const auto
namespace Jde{
	steady_clock::duration _defaultDuration = 1h;
	α Cache::DefaultDuration()ι->steady_clock::duration{ return _defaultDuration; }
	α Cache::Init()ι->void{
		_defaultDuration = Settings::FindDuration( "/cache/default/duration" ).value_or( 1h );
	}
	up<DurationTimer> _timer; mutex _timerMutex;//_timer guarded by _timerMutex. Requires _finalizing set before this is called - the sweep re-arms otherwise.
	α Cache::Shutdown( bool terminate, SL sl )ι->void{
		{
			lg _{ _timerMutex };
			if( !_timer )
				return;
			LOGSL( ELogLevel::Information, sl, ELogTags::App, "Shutting down cache timer" );
			if( terminate )
				_timer = nullptr;
			else
				_timer->Cancel();
		}
		for( uint i=0; i<100; ++i ){//wait for the sweep coroutine to finish - its frame leaks if the executor stops before the cancel handler runs.
			{
				lg _{ _timerMutex };
				if( !_timer )
					return;
			}
			std::this_thread::sleep_for( 1ms );
		}
	}
	absl::Mutex _timeoutsLock;
	std::multimap<steady_clock::time_point,string> _timeouts ABSL_GUARDED_BY(_timeoutsLock);
	absl::Mutex _cacheLock;
	std::flat_map<string,sp<const std::any>> _cache ABSL_GUARDED_BY(_cacheLock);
	ABSL_EXCLUSIVE_LOCKS_REQUIRED(_timeoutsLock) Ω eraseTimeout( str id )ι->void{
		for( auto p = _timeouts.begin(); p!=_timeouts.end(); ++p ){
			if( p->second==id ){
				_timeouts.erase( p );
				break;
			}
		}
	}
	ABSL_SHARED_LOCKS_REQUIRED(_timeoutsLock) Ω getTimer()ι->up<DurationTimer>{
		return _timeouts.empty() || Process::Finalizing()
			? nullptr
      : mu<DurationTimer>( std::max(_timeouts.begin()->first-steady_clock::now(), steady_clock::duration::zero()) );
	}
	ABSL_UNLOCK_FUNCTION(_timerMutex) Ω startTimer( up<DurationTimer>&& timer )ι->DurationTimer::Task{//entered holding _timerMutex.
		ASSERT( !_timer );
		_timer = move( timer );
		_timerMutex.unlock();
		auto _ = co_await *_timer;
		ul timeoutsLock{ _timeoutsLock };
		for( auto p = _timeouts.begin(); p!=_timeouts.end() && p->first<=steady_clock::now(); ){
			{
				ul _{ _cacheLock };
				DBGT( ELogTags::Cache, "[{}]Cache removed", p->second );
				_cache.erase( p->second );
			}
			p = _timeouts.erase( p );
		}
		auto newTimer = getTimer();
		_timerMutex.lock();
		_timer = nullptr;
		if( newTimer && !Process::Finalizing() )//re-checked under _timerMutex - getTimer's read isn't ordered with Shutdown.
			startTimer( move(newTimer) );
		else
		 	_timerMutex.unlock();
	}
	namespace Cache{
		α Internal::Get( str id )ι->sp<const std::any>{
			rl _{ _cacheLock };
			return FindDefault( _cache, id );
		}
		α Internal::Set( str id, sp<std::any> value, optional<steady_clock::duration> duration )ι->sp<const std::any>{
			if( duration && *duration<steady_clock::duration::zero() ){
				Clear( id );//negative duration = don't cache - drop any prior entry so Get can't serve a stale value.
				return value;
			}
			ul timeoutsLock{ _timeoutsLock };//taken before _cacheLock, matching the sweep's nesting - otherwise the sweep can evict a freshly assigned value via the old due timeout.
			bool isNew;
			{
				ul _{ _cacheLock };
				isNew = _cache.insert_or_assign( id, value ).second;
			}
			if( !isNew ) // could have replaced existing duration
				eraseTimeout( id );
			if( duration.has_value() ){
				let deadline = steady_clock::now() + *duration;
				let isEarliest = _timeouts.empty() || deadline<_timeouts.begin()->first;
				_timeouts.emplace( deadline, id );
				_timerMutex.lock();
				if( _timer ){
					if( isEarliest )
						_timer->Cancel();//pending wait completes with operation_aborted; the sweep wakes, re-derives the earliest deadline from _timeouts and re-arms.
					_timerMutex.unlock();
				}
				else{
					auto newTimer = getTimer();
					if( newTimer )
						startTimer( move(newTimer) );
					else
					 	_timerMutex.unlock();
				}
			}
			return value;
		}
	}
	α Cache::Clear( str id )ι->bool{
		ul timeoutsLock{ _timeoutsLock };
		bool erased;
		{
			ul _{ _cacheLock };
			erased = _cache.erase( id )>0;
			DBGT( ELogTags::Cache, "[{}]Cache removed", id );
		}
		if( erased )
			eraseTimeout( id );
		return erased;
	}
}
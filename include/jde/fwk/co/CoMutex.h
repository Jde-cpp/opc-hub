#pragma once
#include <jde/fwk/usings.h>

namespace Jde{
	struct CoMutex; namespace Internal{ struct LockAwait; }
	//CoMutex's std::unique_lock:  default-constructed, moved-from or unlocked, it holds nothing.
	struct CoGuard final{
		CoGuard()ι=default;
		CoGuard( CoGuard&& x )ι:_coMutex{ std::exchange(x._coMutex, nullptr) }{}
		α operator=( CoGuard&& x )ι->CoGuard&{ if( this!=&x ){ unlock(); _coMutex = std::exchange( x._coMutex, nullptr ); } return *this; }
		~CoGuard(){ unlock(); }
		α unlock()ι->void;
		explicit operator bool()Ι{ return _coMutex!=nullptr; }
	private:
		explicit CoGuard( CoMutex& m )ι:_coMutex{ &m }{}
		CoMutex* _coMutex{};
		friend CoMutex; friend Internal::LockAwait;
	};
namespace Internal{
	struct LockAwait final{
		α await_ready()Ι->bool{ return false; }
		α await_suspend( coroutine_handle<> h )ι->bool;//false:  acquired without suspending.
		α await_resume()ι->CoGuard{ return CoGuard{ _coMutex }; }
	private:
		explicit LockAwait( CoMutex& m )ι:_coMutex{ m }{}
		CoMutex& _coMutex;
		coroutine_handle<> _h;
		LockAwait* _next{};//the wait queue runs through the awaiters, which live in their suspended frames.
		friend CoMutex;
	};
}
	//A mutex for coroutines.  Any coroutine can co_await Lock():  nothing goes through the caller's promise, so it doesn't
	//dictate the caller's return type (CLAUDE.md 'Coroutines').  First come, first served - Unlock hands the lock straight
	//to the longest waiter, so neither TryLock nor a new Lock can take it in between.
	struct Γ CoMutex final{
		using LockAwait=Internal::LockAwait;
		~CoMutex();
		α Lock()ι->LockAwait{ return LockAwait{ *this }; }
		α TryLock()ι->CoGuard;//disengaged when held.  Never waits, for a caller whose holder may never resume.
	private:
		α Acquire( LockAwait& waiter )ι->bool;//false:  queued.
		α Unlock()ι->void;
		mutex _mutex;
		bool _locked{};
		LockAwait* _head{};
		LockAwait* _tail{};
		friend CoGuard; friend LockAwait;
	};

	Ξ CoGuard::unlock()ι->void{
		if( auto m = std::exchange(_coMutex, nullptr); m )
			m->Unlock();
	}
namespace Internal{
	Ξ LockAwait::await_suspend( coroutine_handle<> h )ι->bool{
		_h = h;
		return !_coMutex.Acquire( *this );
	}
}}
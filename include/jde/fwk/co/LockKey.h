#pragma once
#include "boost/noncopyable.hpp"
#include <jde/fwk/co/Await.h>

namespace Jde{
	//using namespace Coroutine;
	struct LockKeyAwait;
	struct Γ CoLockGuard final{
		CoLockGuard( string Key, std::variant<uint,coroutine_handle<>> )ι;
		CoLockGuard( CoLockGuard&& rhs )ι;
		α operator=( CoLockGuard&& rhs )ι->CoLockGuard&;
		~CoLockGuard();
	private:
		α Release()ι->void;//pops our queue entry & wakes the next waiter; disengages the guard.
		variant<uint,coroutine_handle<>> Handle;
		string Key;
	};

	//Non-blocking acquire; nullopt when the key is already held.  LockKeyAwait cannot be used to probe: its await_ready
	//enqueues *before* it answers, so abandoning it on a false answer leaves a placeholder in the queue that no
	//CoLockGuard will ever pop - the key would be locked for the rest of the process.  This enqueues only on success.
	//For callers that must not block on a lock whose holder may never resume, e.g. anything running after the executor
	//has been destroyed.
	Γ α TryLockKey( string key )ι->optional<CoLockGuard>;

	struct Γ LockKeyAwait final : TAwait<CoLockGuard>, noncopyable{
		using base=TAwait<CoLockGuard>;
		LockKeyAwait( string key )ι:Key{move(key)}{}
		~LockKeyAwait(){ TRACET( ELogTags::Locks, "({})~LockKeyAwait", Key ); }

		α await_ready()ι->bool override;
		α Suspend()ι->void override;
		α await_resume()ι->CoLockGuard override;
	private:
		uint _index;
		base::Handle _waitHandle{nullptr};
		const string Key;
	};
}
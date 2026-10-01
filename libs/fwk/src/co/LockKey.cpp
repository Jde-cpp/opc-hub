#include <jde/fwk/co/LockKey.h>
#include <deque>
#include <jde/fwk/process/execution.h>

#define let const auto
namespace Jde{

	constexpr ELogTags _tags = ELogTags::Locks;
	flat_map<string,std::deque<std::variant<uint,coroutine_handle<>>>> _coLocks; mutex _coLocksMutex;
	atomic<uint> _lockIndex;

	α TryLockKey( string key )ι->optional<CoLockGuard>{
		lg _( _coLocksMutex );
		auto& locks = _coLocks.try_emplace( key ).first->second;//absent -> empty deque, which is the acquirable state.
		if( locks.size() ){
			TRACE( "({})TryLockKey - held, size={}.", key, locks.size() );
			return nullopt;//nothing enqueued: see the header for why probing with await_ready cannot work.
		}
		let index = ++_lockIndex;
		locks.push_back( index );
		TRACE( "({})TryLockKey - acquired.", key );
		return CoLockGuard{ move(key), index };
	}

	α LockKeyAwait::await_ready()ι->bool{
		lg _( _coLocksMutex );
		auto& locks = _coLocks.try_emplace( Key ).first->second;
		_index = ++_lockIndex;
		locks.push_back( _index );
		let ready = locks.size()==1;
		TRACE( "({})LockKeyAwait::await_ready={} size={} 0={}", Key, ready, locks.size(), locks[0].index() ? "await" : "ready" );
		return locks.size()==1;
	}
	α LockKeyAwait::Suspend()ι->void{
		_coLocksMutex.lock(); ASSERT( _coLocks.find(Key)!=_coLocks.end() );
		auto& locks = _coLocks.find( Key )->second;
		ASSERT( locks.size() );
		if( locks.front().index()==0 && get<uint>(locks.front())==_index ){//earlier holders released between await_ready & here - the lock is ours; a releaser can't resume a uint placeholder.
			_coLocksMutex.unlock();
			TRACE( "({})LockKeyAwait::Suspend - lock acquired before suspend.", Key );
			_h.resume();
			return;
		}
		for( int i=(int)locks.size()-1; !_waitHandle && i>=0; --i ){
			if( locks[i].index()==0 && get<uint>(locks[i])==_index )
				locks[i] = _waitHandle = _h;
		}
		let size = locks.size();
		_coLocksMutex.unlock();
		ASSERT( _waitHandle );
		TRACE( "({})LockKeyAwait::await_suspend size={}", Key, size );
	}
	α LockKeyAwait::await_resume()ι->CoLockGuard{
		return _waitHandle ? CoLockGuard{ Key, _waitHandle } : CoLockGuard{ Key, _index };
	}

	CoLockGuard::CoLockGuard( string key, std::variant<uint,coroutine_handle<>> h )ι:
		Handle{h},
		Key{ move(key) }{
			TRACE( "({})CoLockGuard() index={}", Key, h.index() );
	}
	CoLockGuard::CoLockGuard( CoLockGuard&& rhs )ι:Handle{move(rhs.Handle)}, Key{move(rhs.Key)}{
		rhs.Handle = nullptr;
		rhs.Key.clear();//moved-from small strings can retain their value - dtor guards on Key.empty() & would release the lock twice.
	}

	α CoLockGuard::operator=( CoLockGuard&& x )ι->CoLockGuard&{
		if( this!=&x ){
			Release();//release the held lock - overwriting would leave the key locked forever.
			Handle = x.Handle;
			x.Handle = {};
			Key = move( x.Key );
			x.Key.clear();
		}
		return *this;
	}

	CoLockGuard::~CoLockGuard(){
		Release();
	}
	α CoLockGuard::Release()ι->void{
		if( Key.empty() )
			return;

		lg _( _coLocksMutex );
		auto pLocks = _coLocks.find( Key );
		ASSERT( pLocks!=_coLocks.end() && pLocks->second.size() );
		if( pLocks==_coLocks.end() || pLocks->second.empty() )//ASSERT only logs - popping a missing/empty entry is ub.
			return;
		auto& locks = pLocks->second; //ASSERT( locks[0]==Handle );
		if( locks[0]!=Handle )
			TRACE( "({})~CoLockGuard - unexpected handle at front of queue. locks={} vs handle={} ", Key, locks[0].index() ? "await" : "ready", Handle.index()  ? "await" : "ready" );
		TRACE( "({})~CoLockGuard - pop front.", Key, locks.size() );
		locks.pop_front();
		if( locks.size() ){
			if( locks.front().index()==1 )
				Post( move(get<1>(locks.front())) );
			else
				TRACE( "({})~CoLockGuard - size={}, next is await_ready, should continue.", Key, locks.size() );
		}
		else
			_coLocks.erase( pLocks );//else empty deques accumulate forever, one per key. waiters between await_ready & Suspend hold a queue entry, so an empty deque has no pending users & await_ready re-creates on demand.
		TRACE( "~CoLockGuard( {} )", Key );
		Key.clear(); Handle = {};//disengage - keeps Release idempotent.
	}
}
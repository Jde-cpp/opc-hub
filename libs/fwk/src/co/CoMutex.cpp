#include <jde/fwk/co/CoMutex.h>
#include <jde/fwk/process/execution.h>

namespace Jde{
	CoMutex::~CoMutex(){
		ASSERT( !_locked && !_head );
	}

	α CoMutex::TryLock()ι->CoGuard{
		lg _{ _mutex };
		if( _locked )
			return {};
		_locked = true;
		return CoGuard{ *this };
	}

	α CoMutex::Acquire( LockAwait& waiter )ι->bool{
		lg _{ _mutex };
		if( !_locked ){
			_locked = true;
			return true;
		}
		(_tail ? _tail->_next : _head) = &waiter;
		_tail = &waiter;
		return false;
	}

	α CoMutex::Unlock()ι->void{
		coroutine_handle<> next;
		{
			lg _{ _mutex };
			if( auto waiter = _head; waiter ){
				if( !(_head = waiter->_next) )
					_tail = nullptr;
				next = waiter->_h;//handed over - _locked stays set.
			}
			else
				_locked = false;
		}
		if( next )
			Post( next );//not inline:  a run of unlocks would nest each waiter on the releaser's stack, and on its thread.
	}
}
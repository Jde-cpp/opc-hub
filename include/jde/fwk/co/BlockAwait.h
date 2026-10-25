#pragma once
#include <absl/synchronization/notification.h>
#include <jde/fwk/co/Await.h>

namespace Jde::Internal{
	//The blocking half of the bridge.  Wait() never gives up - a bounded wait would turn every legitimately long sync call
	//(schema sync, LocalQL::Upsert) into a spurious failure - but it warns on an interval naming the awaitable's source
	//location, so a response that is dropped instead of delivered leaves a trail rather than 7 minutes of silence.
	//reviews/gateway-review.md #36.
	struct Γ BlockAwaitSync{
		α Signal()ι->void;
		//stallLevel caps the stall lines - Warning at the first interval, Critical from the second - for a wait the caller knows
		//to be long: the OpcServer's startup blocked on a hub that is not up yet, whose retry warnings already say why (a
		//hang would read the same way otherwise - install-issues, the 09-15 retry table).  The line still comes every interval.
		α Wait( SL sl, ELogLevel stallLevel=ELogLevel::Critical )ι->void;
	private:
		absl::Notification _done;
	};

	//On the blocked thread's stack:  the waiter can wake & return while Signal is still inside Notify, and absl::Notification
	//allows destroying it then.
	template<class TResult>
	struct BlockAwaitState : BlockAwaitSync{
		optional<TResult> Result;
		up<Exception> Error;
	};

	//The glue's return type:  an IAwait-family awaitable resumes only a coroutine of its own ::Task (CLAUDE.md 'Coroutines');
	//the Any family has no ::Task and resumes any coroutine.
	Τ struct BlockGlue{ using type = VoidTask; };
	template<class T> requires requires{ typename T::Task; }
	struct BlockGlue<T>{ using type = T::Task; };

	//One bridge for both families.  A void awaitable parks a monostate state - there is no value to store.
	template<class TAwait, class TResult>
	α BlockAwaitExecute( TAwait& a, BlockAwaitState<TResult>* s )ι->BlockGlue<TAwait>::type{
		try{
			if constexpr( std::is_same_v<TResult,std::monostate> )
				co_await a;
			else
				s->Result = co_await a;
		}
		catch( Exception& e2 ){
			s->Error = e2.Move();
		}
		s->Signal();
	}
}
namespace Jde{
	//TResult defaults to what the co_await gives;  the callers that spell it, BlockAwait<X,Y>, get the same function.
	template<class TAwait, class TResult=AwaitResult<TAwait>>
	α BlockAwait( TAwait&& a, ELogLevel stallLevel=ELogLevel::Critical )ε->TResult{
		using TState = std::conditional_t<std::is_void_v<TResult>, std::monostate, TResult>;
		Internal::BlockAwaitState<TState> s;
		const auto sl = a.Source();//copied before the co_await - the awaitable is the caller's only clue to what a stalled wait is waiting on.
		Internal::BlockAwaitExecute<std::remove_reference_t<TAwait>,TState>( a, &s );
		s.Wait( sl, stallLevel );
		if( s.Error )
			s.Error->Throw();
		if constexpr( !std::is_void_v<TResult> )
			return move( *s.Result );
	}
}
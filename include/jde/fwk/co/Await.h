#pragma once
#ifndef AWAIT_H
#define AWAIT_H
#include <condition_variable>
#include "Task.h"

namespace Jde{
	//What VoidAwait and IAwait share - the task-typed handle, the diagnostic await_suspend, the promise-routed
	//ResumeExp/SetError, Resume, Source and the rethrow.  Templated on the task because the handle type is the whole
	//point: an awaitable dictates its caller's return type (CLAUDE.md 'Coroutines').
	template<class TTask>
	struct AwaitBase{
		using TPromise = TTask::promise_type;
		using Task=TTask;
		using Handle=coroutine_handle<TPromise>;
		AwaitBase( SRCE )ι:_sl{sl}{}
		β await_ready()ι->bool{ return false; }
		α await_suspend( Handle h )ι->void{ _h=h; Suspend(); }
		//Diagnostic only - a matching caller selects the non-template overload above; a mismatched one lands here and the static_assert names the rule instead of 'no viable conversion from coroutine_handle<...>'.
		Τ α await_suspend( coroutine_handle<T> )ι->void{
			static_assert( std::is_same_v<T,TPromise>, "An awaitable dictates its caller's return type: a coroutine may only co_await awaitables whose ::Task is its own return type (CLAUDE.md 'Coroutines'). Split into a hand-off chain, or co_await Any(awaitable) from co/AnyAwait.h." );
		}
		α ResumeExp( Exception&& e )ι{
			ASSERT( Promise() );
			Promise()->ResumeExp( move(e), _h );
		}
		α ResumeExp( std::runtime_error&& e )ι{//runtime_error, matching IPromise - a logic_error (stoi/stod) is a caller bug, not a result.
			ASSERT( Promise() );
			Promise()->ResumeExp( move(e), _h );
		}
		α Resume()ι{ ASSERT(_h); auto h=_h; _h=nullptr; h.resume(); }
		α Source()ι->SL{ return _sl; }
	protected:
		α SetError( Exception&& e )ι{ ASSERT(Promise()); Promise()->SetExp( move(e) ); }
		β Suspend()ι->void=0;
		α CheckException()ε->void{//rethrows the promise's stored exception, if any - the awaited value is read afterwards by the subclass.
			if( up<Exception> e = Promise() ? Promise()->MoveExp() : nullptr; e ){
				_h = nullptr;
				e->Throw();
			}
		}
		α Promise()->TPromise*{ return _h ? &_h.promise() : nullptr; }
		Handle _h{};
		SL _sl;
	};

	struct VoidAwait : AwaitBase<VoidTask>{
		using AwaitBase<VoidTask>::AwaitBase;
		β await_resume()ε->void{ CheckException(); }
	};

	template<class TResult,class TTask>
	struct IAwait : AwaitBase<TTask>{
		using base = AwaitBase<TTask>;
		using base::base;
		β await_resume()ε->TResult = 0;
	protected:
		α Suspend()ι->void override{}//a default here, none in VoidAwait: typed awaitables may complete from await_ready and never suspend.
	};

	template<class Result,class TTask=Jde::TTask<Result>>
	struct TAwait : IAwait<Result,TTask>{
		using base = IAwait<Result,TTask>;
		TAwait( SRCE )ι:base{sl}{}
		virtual ~TAwait()=0;
		α await_resume()ε->Result;

		α Emplaced()ι{ return base::Promise() && base::Promise()->Emplaced(); }
		α SetValue( Result&& r )ι{ ASSERT(base::Promise()); base::Promise()->SetValue( move(r) ); }
		α Resume( Result&& r )ι{ ASSERT(base::Promise()); base::Promise()->Resume( std::move(r), base::_h ); }
		α ResumeScaler( Result r )ι{ ASSERT(base::Promise()); base::Promise()->Resume( std::move(r), base::_h ); } //win CoGuard doesn't have a copy constructor.
	};
	template<class Result,class TTask>
	TAwait<Result,TTask>::~TAwait(){};

	template<class Result,class TTask>
	α TAwait<Result,TTask>::await_resume()ε->Result{
		base::CheckException();
		if( !base::Promise() )
			throw Jde::Exception{ SRCE_CUR, Jde::ELogLevel::Critical, "promise is null" };
		if( !base::Promise()->Value() )
			throw Jde::Exception{ SRCE_CUR, Jde::ELogLevel::Critical, "Value is null" };
		Result result( std::move(*base::Promise()->Value()) );  //don't use initializer list.
		base::_h = nullptr;
		return result;
	}

	template<class Result,class TExecuteResult=void, class TTask=Jde::TTask<Result>>
	struct TAwaitEx : TAwait<Result,TTask>{
		using TBase = TAwait<Result,TTask>;
		TAwaitEx( SRCE )ι:TBase{sl}{}
		α Suspend()ι->void override{ Execute(); }
		β Execute()ι->TExecuteResult=0;
	};

	//An already-failed typed awaitable:  co_await throws `e` without suspending - how a hook refuses (IQLHook returns one).  The
	//IAwait family cannot pre-complete with a *value*, the awaiting promise is the mailbox, but it can pre-fail; the Any-family
	//equivalent is AnyCompletedAwait (AnyAwait.h).
	template<class Result,class TTask=Jde::TTask<Result>>
	struct ExceptionAwait final : TAwait<Result,TTask>{
		ExceptionAwait( up<Exception>&& e, SRCE )ι:TAwait<Result,TTask>{ sl }, _exception{ move(e) }{}
		α await_ready()ι->bool override{ return true; }
		α await_resume()ε->Result override{ _exception->Throw(); return {}; }
	private:
		up<Exception> _exception;
	};

	//msvc multiple defined symbols without
	struct Γ StringAwait : TAwait<string>{
		StringAwait( SRCE )ι:TAwait<string>{ sl }{}
	};

	struct Γ UInt32Await : TAwait<uint32>{
		UInt32Await( SRCE )ι:TAwait<uint32>{ sl }{}
	};


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
		std::mutex _mutex;
		std::condition_variable _cv;
		bool _done{};
	};

	//shared between the blocked thread & the coroutine: the waiter can wake & return between the signal and the notify,
	//so stack-owned state would be destroyed while notify_all still touches it - the coroutine frame co-owns it instead.
	template<class TResult>
	struct BlockAwaitState : BlockAwaitSync{
		optional<TResult> Result;
		up<Exception> Error;
	};

	//One bridge for both kinds.  A void awaitable parks a monostate state - there is no value to store - and BlockVoidAwait
	//is that instantiation; it used to be a second copy of these two functions.
	template<class TAwait, class TResult>
	α BlockAwaitExecute( TAwait& a, sp<BlockAwaitState<TResult>> s )ι->TAwait::Task{
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

	template<class TAwait, class TResult>
	α BlockAwait( TAwait&& a, ELogLevel stallLevel=ELogLevel::Critical )ε->TResult{
		auto s = ms<BlockAwaitState<TResult>>();
		const auto sl = a.Source();//copied before the co_await - the awaitable is the caller's only clue to what a stalled wait is waiting on.
		BlockAwaitExecute<TAwait,TResult>( a, s );
		s->Wait( sl, stallLevel );
		if( s->Error )
			s->Error->Throw();
		if constexpr( std::is_same_v<TResult,std::monostate> )
			return {};
		else
			return move( *s->Result );
	}

	Ξ BlockVoidAwait( VoidAwait&& a, ELogLevel stallLevel=ELogLevel::Critical )ε->void{ BlockAwait<VoidAwait,std::monostate>( move(a), stallLevel ); }

	Ŧ BlockTAwait( TAwait<T>&& a, ELogLevel stallLevel=ELogLevel::Critical )ε->T{
		return BlockAwait<TAwait<T>,T>( move(a), stallLevel );
	}
}
#endif
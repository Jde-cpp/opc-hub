#pragma once
//#include <condition_variable>
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

	//An already-completed typed awaitable:  `body` runs in await_ready, and co_await returns what it returned - or throws what it
	//threw, as its own type - without suspending.  The value half of ExceptionAwait, for a hook that answers synchronously.
	template<class Result,class TTask=Jde::TTask<Result>>
	struct CompletedAwait final : TAwait<Result,TTask>{
		CompletedAwait( std::function<Result()>&& body, SRCE )ι:TAwait<Result,TTask>{ sl }, _body{ move(body) }{}
		α await_ready()ι->bool override{
			try{
				_result = _body();
			}
			catch( Exception& e ){
				_exception = e.Move();
			}
			catch( std::runtime_error& e ){
				_exception = mu<Exception>( move(e) );
			}
			return true;
		}
		α await_resume()ε->Result override{
			if( _exception )
				_exception->Throw();
			return move( *_result );
		}
	private:
		std::function<Result()> _body;
		optional<Result> _result;
		up<Exception> _exception;
	};

	//msvc multiple defined symbols without
	struct Γ StringAwait : TAwait<string>{
		StringAwait( SRCE )ι:TAwait<string>{ sl }{}
	};

	struct Γ UInt32Await : TAwait<uint32>{
		UInt32Await( SRCE )ι:TAwait<uint32>{ sl }{}
	};

	//Either family - this one and AnyAwait.h's.
	Τ using AwaitResult = decltype( std::declval<std::remove_reference_t<T>&>().await_resume() );//what a co_await of T gives;  T may be a reference.
	Τ using AwaitValue = std::conditional_t<std::is_void_v<AwaitResult<T>>, std::monostate, AwaitResult<T>>;//storable:  void as std::monostate.
}
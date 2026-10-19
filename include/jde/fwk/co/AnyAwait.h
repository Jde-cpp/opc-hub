#pragma once
#ifndef ANY_AWAIT_H
#define ANY_AWAIT_H
#include "Task.h"
#include "Await.h"

namespace Jde{
	//Secondary awaitable machinery. The VoidAwait/IAwait family (Await.h) fixes await_suspend to its own task's
	//handle and uses the awaiting coroutine's promise as the value mailbox, so a coroutine can only co_await
	//awaitables whose ::Task is its own return type - mixing types forces the one-coroutine-per-awaitable
	//hand-off chain. These awaitables carry their own result/exception and hold a type-erased continuation, so
	//any coroutine can co_await them regardless of its return type; Any() wraps an existing awaitable for the
	//same purpose, and All() (AllAwait.h) awaits several at once. Prefer these for new cross-type awaits instead of adding another chain.
	//What the void and typed awaitables share: the erased continuation, the error slot, the source location, and every
	//method that does not touch a result.  Non-template, so one definition serves AnyAwait<T> for every T.
	struct AnyAwaitBase{
		AnyAwaitBase( SRCE )ι:_sl{sl}{}
		AnyAwaitBase( AnyAwaitBase&& )ι=default;//before it suspends only - All() owns its rvalue slots so;  suspended, its continuation holds it and it never moves.  No copy:  the error slot is unique.
		virtual ~AnyAwaitBase()=default;
		β await_ready()ι->bool{ return false; }
		α await_suspend( coroutine_handle<> h )ι->void{ _h=h; Suspend(); }	//any promise's handle converts.
		//Resume/ResumeExp may run the awaiting coroutine to completion inline, destroying this awaitable - they must be the caller's last use of `this`.
		α ResumeExp( Exception&& e )ι->void{ _error=e.Move(); ResumeHandle(); }	//virtual Move keeps the subclass - await_resume rethrows the dynamic type.
		α ResumeExp( runtime_error&& e )ι->void{
			if( auto p = dynamic_cast<Exception*>(&e); p )
				ResumeExp( move(*p) );
			else{
				_error = mu<Exception>( move(e) );
				ResumeHandle();
			}
		}
		α Source()Ι->SL{ return _sl; }
	protected:
		β Suspend()ι->void=0;
		α ResumeHandle()ι->void{ ASSERT(_h); auto h=_h; _h=nullptr; h.resume(); }
		α CheckException()ε->void{
			if( _error ){
				auto e = move(_error);
				e->Throw();
			}
		}
		coroutine_handle<> _h{};
		up<Exception> _error;
		SL _sl;
	};

	struct AnyVoidAwait : AnyAwaitBase{
		using AnyAwaitBase::AnyAwaitBase;
		α await_resume()ε->void{ CheckException(); }
		α Resume()ι->void{ ResumeHandle(); }
	};

	//An already-completed void awaitable: co_await returns immediately - throwing `error` if one was given - without suspending.
	//For synchronous implementations of an interface whose contract is an awaitable (e.g. a local check beside a remote one).
	struct AnyCompletedAwait final : AnyVoidAwait{
		AnyCompletedAwait( up<Exception> error={}, SRCE )ι:AnyVoidAwait{sl}{ _error=move(error); }
		α await_ready()ι->bool override{ return true; }
	protected:
		α Suspend()ι->void override{}//unreachable: await_ready is true.
	};

	template<class TResult>
	struct AnyAwait : AnyAwaitBase{
		using AnyAwaitBase::AnyAwaitBase;
		α await_resume()ε->TResult{
			CheckException();
			if( !_result )
				throw Exception{ _sl, {ELogLevel::Critical}, "Resumed without a value." };
			return move( *_result );
		}
		α Resume( TResult&& r )ι->void{ _result=move(r); ResumeHandle(); }
	protected:
		optional<TResult> _result;
	};

	Τ using AwaitResult = decltype( std::declval<std::remove_reference_t<T>&>().await_resume() );//what a co_await of T gives;  T may be a reference.
	Τ using AwaitValue = std::conditional_t<std::is_void_v<AwaitResult<T>>, std::monostate, AwaitResult<T>>;//storable:  void as std::monostate.
	template<class R> using AnyAwaitFor = std::conditional_t<std::is_void_v<R>, AnyVoidAwait, AnyAwait<R>>;

	Τ struct AnyAdapter;
	Τ Ξ AnyAdapterExecute( AnyAdapter<T>& a )ι->typename std::remove_reference_t<T>::Task;

	//Bridges a VoidAwait/IAwait-family awaitable to a foreign coroutine: Suspend launches a glue coroutine of
	//the inner's own task type (BlockAwaitExecute reshaped - the thread block replaced by resuming the erased
	//continuation). T is the inner's value type when Any() is given an rvalue, a reference type when given an
	//lvalue - the lvalue must outlive the co_await, which it does when it lives in the awaiting frame.
	Τ struct AnyAdapter final : AnyAwaitFor<AwaitResult<T>>{
		using TInner = std::remove_reference_t<T>;
		using R = AwaitResult<T>;
		using base = AnyAwaitFor<R>;
		AnyAdapter( T&& inner, SRCE )ι: base{sl}, _inner{ FWD(inner) }{}
		α Inner()ι->TInner&{ return _inner; }
	protected:
		α Suspend()ι->void override{ AnyAdapterExecute( *this ); }	//discards the eager, self-destroying glue task - the BlockVoidAwaitExecute pattern.
	private:
		T _inner;
	};

	Ŧ Any( T&& inner, SRCE )ι->AnyAdapter<T>{ return AnyAdapter<T>{ FWD(inner), sl }; }

	//The blocking bridge for this family:  BlockAwait (Await.h) launches its glue as the inner's ::Task, which these do not have, so
	//the glue is a VoidTask.  Same contract otherwise - Wait() never gives up, but warns on an interval naming the awaitable's source.
	template<class TAwait, class TResult>
	α BlockAnyExecute( TAwait& a, BlockAwaitState<TResult>* s )ι->VoidTask{
		try{
			if constexpr( std::is_same_v<TResult,std::monostate> )
				co_await a;
			else
				s->Result = co_await a;
		}
		catch( Exception& e ){
			s->Error = e.Move();
		}
		s->Signal();
	}
	template<class TAwait>
	α BlockAny( TAwait&& a )ε->AwaitResult<TAwait>{
		using R = AwaitResult<TAwait>;
		using S = AwaitValue<TAwait>;
		BlockAwaitState<S> s;
		const auto sl = a.Source();
		BlockAnyExecute<std::remove_reference_t<TAwait>,S>( a, &s );
		s.Wait( sl );
		if( s.Error )
			s.Error->Throw();
		if constexpr( !std::is_void_v<R> )
			return move( *s.Result );
	}

	Τ Ξ AnyAdapterExecute( AnyAdapter<T>& a )ι->typename std::remove_reference_t<T>::Task{
		using R = typename AnyAdapter<T>::R;
		//Resume/ResumeExp is the last use of `a` on every path, and nothing may escape: an exception reaching
		//the glue promise would be swallowed there and strand the awaiting coroutine forever.
		try{
			if constexpr( std::is_void_v<R> ){
				co_await a.Inner();
				a.Resume();
			}
			else
				a.Resume( co_await a.Inner() );
		}
		catch( Exception& e ){
			a.ResumeExp( move(e) );
		}
		catch( std::runtime_error& e ){
			a.ResumeExp( move(e) );
		}
		catch( ... ){
			a.ResumeExp( Exception{"Unknown exception from wrapped awaitable.", {ELogLevel::Critical}} );
		}
	}
	//A coroutine any coroutine can co_await, which runs only once awaited:  co_await starts its body (symmetric transfer) and the
	//body's end resumes the awaiter, rethrowing what escaped it as AnyVoidAwait does.  For work picked now and awaited later - a
	//VoidTask starts at once, detached, and can answer only through a reference to its awaiter.  Its body co_awaits this family
	//alone (Any() for the rest).  Move-only:  it owns the frame, destroyed with it, awaited or not.
	struct AnyVoidTask final{
		struct promise_type;
		struct FinalAwait final{
			α await_ready()noexcept->bool{ return false; }
			α await_suspend( coroutine_handle<promise_type> h )noexcept->coroutine_handle<>;
			α await_resume()noexcept->void{}
		};
		struct promise_type final{
			α get_return_object()ι->AnyVoidTask{ return AnyVoidTask{ coroutine_handle<promise_type>::from_promise(*this) }; }
			α initial_suspend()ι->std::suspend_always{ return {}; }
			α final_suspend()noexcept->FinalAwait{ return {}; }
			α return_void()ι->void{}
			α unhandled_exception()ι->void;
			coroutine_handle<> Awaiter;
			up<Exception> Error;
		};
		AnyVoidTask( AnyVoidTask&& x )ι:_h{ std::exchange(x._h, {}) }{}
		α operator=( AnyVoidTask&& x )ι->AnyVoidTask&{ if( this!=&x ){ destroy(); _h = std::exchange( x._h, {} ); } return *this; }
		~AnyVoidTask(){ destroy(); }
		α await_ready()Ι->bool{ return false; }
		α await_suspend( coroutine_handle<> awaiter )ι->coroutine_handle<>{ ASSERT( _h && !_h.done() ); _h.promise().Awaiter = awaiter; return _h; }
		α await_resume()ε->void{
			if( auto e = move(_h.promise().Error); e )
				e->Throw();
		}
	private:
		explicit AnyVoidTask( coroutine_handle<promise_type> h )ι:_h{ h }{}
		α destroy()ι->void{ if( _h ) _h.destroy(); }
		coroutine_handle<promise_type> _h;
	};
	Ξ AnyVoidTask::FinalAwait::await_suspend( coroutine_handle<promise_type> h )noexcept->coroutine_handle<>{ return h.promise().Awaiter; }
	Ξ AnyVoidTask::promise_type::unhandled_exception()ι->void{
		try{
			throw;
		}
		catch( Exception& e ){
			Error = e.Move();
		}
		catch( std::runtime_error& e ){
			Error = mu<Exception>( move(e) );
		}
		catch( ... ){
			Error = Exception{ "Unknown exception from an AnyVoidTask.", {ELogLevel::Critical} }.Move();
		}
	}
}
#endif
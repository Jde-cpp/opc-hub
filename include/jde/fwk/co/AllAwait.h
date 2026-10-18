#pragma once
#ifndef ALL_AWAIT_H
#define ALL_AWAIT_H
#include <array>
#include <atomic>
#include "AnyAwait.h"

namespace Jde{
	//Several at once - the Any family's (AnyAwait.h) combinator.  All( a, b, … ) starts every slot from Suspend and resumes its awaiter
	//once, after the last has settled, with a tuple of their results - std::monostate for a void slot, so the positions line up with the
	//arguments - or the first failing slot's exception in slot order;  the others are destroyed with it, and an Exception logs itself
	//when destroyed unlogged.  Slots are either family, unwrapped:  an IAwait-family slot's glue coroutine is its own ::Task (the pairing
	//rule, CLAUDE.md 'Coroutines'), an Any-family slot's a VoidTask.  Rvalue slots are owned, lvalues referenced and must outlive the
	//co_await, as Any() has it;  the source location is the first slot's - a SRCE can't follow a pack.
	//Nothing cancels:  neither family can, and an in-flight slot resumes into its glue, which writes into this - so a failure waits for
	//the others like any other outcome;  resuming early would free what they still use.  The glues hold `this`, so the combinator never
	//moves:  All() returns it by guaranteed elision, and the awaiting frame keeps it for the co_await.
	Τ using AwaitResult = decltype( std::declval<std::remove_reference_t<T>&>().await_resume() );
	Τ using AllSlotValue = std::conditional_t<std::is_void_v<AwaitResult<T>>, std::monostate, AwaitResult<T>>;
	Τ struct AllGlueTask{ using type = VoidTask; };//Any family:  no ::Task, any coroutine may await it.
	Τ requires requires{ typename T::Task; } struct AllGlueTask<T>{ using type = typename T::Task; };//IAwait family:  the pairing rule.
	template<size_t I, class... Ts> using AllGlue = typename AllGlueTask<std::remove_reference_t<std::tuple_element_t<I,tuple<Ts...>>>>::type;

	template<class... Ts> struct AllAwait;
	template<size_t I, class... Ts> Ξ AllAwaitExecute( AllAwait<Ts...>& a )ι->AllGlue<I,Ts...>;

	template<class... Ts>
	struct AllAwait final : AnyAwait<tuple<AllSlotValue<Ts>...>>{
		static_assert( sizeof...(Ts)>0, "All() needs an awaitable." );
		using TResult = tuple<AllSlotValue<Ts>...>;
		using base = AnyAwait<TResult>;
		template<size_t I> using TInner = std::remove_reference_t<std::tuple_element_t<I,tuple<Ts...>>>;
		AllAwait( Ts&&... inners )ι: base{ FirstSource(inners...) }, _inners{ FWD(inners)... }{}
		template<size_t I> α Inner()ι->TInner<I>&{ return std::get<I>( _inners ); }
		//A glue's one hand-off:  record, then count down.  The call that brings the count to zero finishes the await and may destroy this,
		//so it is the glue's last use of `a`;  for every other slot the fetch_sub inside is - another slot may be finishing concurrently.
		template<size_t I> α Settle( AllSlotValue<std::tuple_element_t<I,tuple<Ts...>>>&& v )ι->void{ std::get<I>(_values) = move(v); Settled(); }
		template<size_t I> α SettleExp( up<Exception>&& e )ι->void{ _errors[I] = move(e); Settled(); }
	protected:
		α Suspend()ι->void override{ Launch( std::index_sequence_for<Ts...>{} ); }
	private:
		Ω FirstSource( auto& first, auto&... )ι->SL{ return first.Source(); }
		//Left to right.  The count reaches zero only once the last slot is launched, so `this` is alive for every launch;  the last may
		//destroy it - inline when every slot completes synchronously, as sqlite's selects do, or from another thread the moment it returns -
		//so nothing follows the fold.
		template<size_t... Is> α Launch( std::index_sequence<Is...> )ι->void{ ( AllAwaitExecute<Is>(*this), ... ); }
		α Settled()ι->void{
			if( _remaining.fetch_sub(1, std::memory_order_acq_rel)!=1 )//acq_rel:  the last sees every slot's write.
				return;
			for( auto& e : _errors ){
				if( e ){//first by slot, not by when it failed;  the others go with this.
					base::_error = move( e );
					base::ResumeHandle();
					return;
				}
			}
			base::Resume( Collect(std::index_sequence_for<Ts...>{}) );
		}
		template<size_t... Is> α Collect( std::index_sequence<Is...> )ι->TResult{ return TResult{ move(*std::get<Is>(_values))... }; }
		tuple<Ts...> _inners;
		tuple<optional<AllSlotValue<Ts>>...> _values;
		std::array<up<Exception>,sizeof...(Ts)> _errors;
		std::atomic<uint> _remaining{ sizeof...(Ts) };//N from construction, not counted up at launch:  a slot may settle inside its own launch.
	};

	template<class... Ts> α All( Ts&&... inners )ι->AllAwait<Ts...>{ return AllAwait<Ts...>{ FWD(inners)... }; }

	template<size_t I, class... Ts>
	Ξ AllAwaitExecute( AllAwait<Ts...>& a )ι->AllGlue<I,Ts...>{
		using R = AwaitResult<typename AllAwait<Ts...>::template TInner<I>>;
		//Settle/SettleExp is the last use of `a` on every path, and nothing may escape - as AnyAdapterExecute.
		try{
			if constexpr( std::is_void_v<R> ){
				co_await a.template Inner<I>();
				a.template Settle<I>( std::monostate{} );
			}
			else
				a.template Settle<I>( co_await a.template Inner<I>() );
		}
		catch( Exception& e ){
			a.template SettleExp<I>( e.Move() );
		}
		catch( std::runtime_error& e ){
			a.template SettleExp<I>( mu<Exception>(move(e)) );
		}
		catch( ... ){
			a.template SettleExp<I>( mu<Exception>("Unknown exception from wrapped awaitable.", ExceptionArgs{ELogLevel::Critical}) );
		}
	}
}
#endif
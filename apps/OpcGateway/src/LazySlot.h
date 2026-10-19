#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/UAException.h>

namespace Jde::Opc::Gateway{
	//What NodeIndex, EnumTypeCache and AggregateFunctions share:  a value fetched on first use - later callers park on the fetch
	//in flight - then answered from memory.  It holds no lock:  the owner's mutex guards it, and Finish hands back the resumes
	//to run once that lock is released.  T=void for an owner that keeps the value itself (NodeIndex's entries).
	template<class T>
	struct LazySlot final{
		using Waiter = AnyAwaitFor<T>;
		using TValue = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
		enum class EState : uint8{ Empty, Fetching, Ready, Failed };
		//The waiters Finish took and their answer.  Run outside the owner's lock:  a resume may run its awaiter to completion
		//inline, back into the owner, and is the last use of the waiter.
		struct [[nodiscard]] Resumes final{
			α Run()ι->void{
				for( auto* waiter : Waiters )
					Answer( *waiter, Value, Error.get() );
			}
			vector<Waiter*> Waiters;
			TValue Value{};
			up<Exception> Error;
		};

		α State()Ι->EState{ return _state; }
		α Value()Ι->const TValue&{ return _value; }
		//Parks `waiter` on the fetch in flight, or answers it from a Ready or Failed slot - posted, never inside await_suspend.  A
		//null waiter only starts.  `refetch` fetches again over a Ready or Failed slot.  True when the caller is to launch the fetch.
		α Start( Waiter* waiter, bool refetch=false )ι->bool{
			bool launch{};
			if( _state==EState::Empty || (refetch && _state!=EState::Fetching) ){
				_state = EState::Fetching;
				launch = true;
			}
			if( _state==EState::Fetching ){
				if( waiter )
					_waiters.push_back( waiter );
			}
			else if( waiter )
				Post( [waiter, value=_value, error=_error ? Copy(*_error) : up<Exception>{}]{ Answer( *waiter, value, error.get() ); } );
			return launch;
		}
		//The fetch's outcome:  Ready with `value`, or Failed with `error` for later Starts to answer - unless `forget`, which
		//empties the slot for the next Start to fetch again.
		α Finish( TValue value, up<Exception> error, bool forget=false )ι->Resumes{
			Resumes y{ move(_waiters) };
			_waiters.clear();
			if( error ){
				_state = forget ? EState::Empty : EState::Failed;
				_error = forget ? up<Exception>{} : Copy( *error );
				y.Error = move( error );
			}
			else{
				_state = EState::Ready;
				_error.reset();
				_value = value;
				y.Value = move( value );
			}
			return y;
		}
		//A silent copy for each waiter - the original logs, once.  Keeps the type:  Exception{e} slices a UAException's status
		//off, and that status is what the SPA's notice shows.  ExternalException's copy ctor is already NoLog.
		Ω Copy( const Exception& e )ι->up<Exception>{
			if( auto ua = dynamic_cast<const UAException*>(&e); ua )
				return mu<UAException>( *ua );
			return mu<Exception>( e.What(), ExceptionArgs{ELogLevel::NoLog, e.Tags, e.Code(), e.HttpStatus()}, e.Source() );
		}
	private:
		Ω Answer( Waiter& waiter, const TValue& value, const Exception* error )ι->void{
			if( error )
				waiter.ResumeExp( move(*Copy(*error)) );
			else if constexpr( std::is_void_v<T> )
				waiter.Resume();
			else
				waiter.Resume( TValue{value} );
		}
		EState _state{ EState::Empty };
		TValue _value{};
		up<Exception> _error;
		vector<Waiter*> _waiters;
	};
}
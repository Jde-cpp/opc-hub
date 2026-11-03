#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/process/execution.h>

namespace Jde::Opc::Gateway{
	//`work` handed to `Run`, which runs it somewhere and passes what it throws to its second argument on the executor;  the awaiter
	//is resumed on the executor with what `work` returned or threw.  The Any family, so any coroutine may co_await it.  `this` lives
	//in the awaiting frame, which holds until the resume.
	template<class T, auto Run>
	struct RunAwait final : AnyAwaitFor<T>{
		using base = AnyAwaitFor<T>;
		RunAwait( absl::AnyInvocable<T()> work, SRCE )ι:base{ sl }, _work{ std::move(work) }{}
	protected:
		α Suspend()ι->void override{
			Run( [this]{
				if constexpr( std::is_void_v<T> ){
					_work();
					Jde::Post( [this]{ base::Resume(); } );
				}
				else
					Jde::Post( [this, result=_work()]()mutable{ base::Resume( std::move(result) ); } );
			}, [this]( up<Exception> e ){ base::ResumeExp( std::move(e) ); }, base::_sl );
		}
	private:
		absl::AnyInvocable<T()> _work;
	};

	Ξ RunOnExecutor( absl::AnyInvocable<void()> work, absl::AnyInvocable<void(up<Exception>)> onError, SL )ι->void{
		Post( [work=std::move(work), onError=std::move(onError)]()mutable{
			try{
				work();
			}
			catch( std::runtime_error& e ){
				onError( Exception::From(std::move(e)) );
			}
		} );
	}
	//Runs `work` on the executor - UAStrandAwait's twin for synchronous work that isn't the client's, whose locks or files the
	//awaiting strand isn't to wait on.
	Τ using ExecutorAwait = RunAwait<T, RunOnExecutor>;
}
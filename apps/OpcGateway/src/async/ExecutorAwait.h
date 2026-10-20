#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/process/execution.h>

namespace Jde::Opc::Gateway{
	//Runs `f` on the executor and resumes the awaiter with what it returns or throws - UAStrandAwait's twin for synchronous work
	//that isn't the client's, whose locks or files the awaiting strand isn't to wait on.  The Any family, so any coroutine may
	//co_await it.
	Τ struct ExecutorAwait final : AnyAwaitFor<T>{
		using base = AnyAwaitFor<T>;
		ExecutorAwait( function<T()> f, SRCE )ι:base{ sl }, _f{ move(f) }{}
	protected:
		α Suspend()ι->void override{
			Post( [this]{
				try{
					if constexpr( std::is_void_v<T> ){
						_f();
						this->Resume();
					}
					else
						this->Resume( _f() );
				}
				catch( runtime_error& e ){
					this->ResumeExp( std::move(e) );
				}
			} );
		}
	private:
		function<T()> _f;
	};
}
#pragma once
#include <absl/functional/any_invocable.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/process/execution.h>

namespace Jde::Opc::Gateway{
	//The blocking-work pool (#353):  the threads a client's build and connect run on - the slug's certificate issued under
	//Crypto::EnsureKeyCertificate's lock, trustedCertDirs scanned, the host resolved and probed for up to three seconds
	//(UAClient::ReachableUrl) - which used to run on the executor thread that awaited the connect, the same threads that run every
	//client strand and every HTTP and WebSocket handler, so a few connects to a host with slow DNS or no route stalled them all.
	//ConnectAwait and TestConnectAwait post here;  discovery (docs/clients/spec.md *Discovery*) joins them.  /workers/blocking/threads
	//of them, two by default, started on the first job.  A job answers its awaiter by posting to the executor, so the thread is free
	//for the next job the moment the blocking work is through.
	namespace BlockingPool{
		α Post( absl::AnyInvocable<void()> work )ι->void;//runs `work` on a pool thread - inline, on the caller's, once Stop has run:  a job begins with ThrowIfShuttingDown and is short then.
		α Stop()ι->void;//Shutdown's, before UAClient::Shutdown:  what is queued runs and the threads are joined, so no client is built after the registry's snapshot.
		α Threads()ι->uint;//the configured count.
		//The refusal every job opens with:  a client built once the process is shutting down would outlive UAClient::Shutdown's
		//snapshot of the registry and nothing would stop it (the reconnect chains had the same gap, subscription-disconnect #3).
		α ThrowIfShuttingDown( SL sl )ε->void;
	}

	//`work` on the blocking pool, and the awaiter resumed on the executor with what it returned, or threw - as after any other
	//await, so nothing downstream runs on a pool thread.  An AnyAwait:  any coroutine can co_await it, whatever its return type.
	//`this` lives in the awaiting frame, which holds until the resume.
	template<class T=void>
	struct BlockingAwait final : AnyAwaitFor<T>{
		using base = AnyAwaitFor<T>;
		BlockingAwait( absl::AnyInvocable<T()> work, SRCE )ι: base{sl}, _work{ std::move(work) }{}
	protected:
		α Suspend()ι->void override{
			BlockingPool::Post( [this]{
				try{
					BlockingPool::ThrowIfShuttingDown( base::_sl );
					if constexpr( std::is_void_v<T> ){
						_work();
						Jde::Post( [this]{ base::Resume(); } );
					}
					else{
						auto result = _work();
						Jde::Post( [this, result=std::move(result)]()mutable{ base::Resume( std::move(result) ); } );
					}
				}
				catch( std::runtime_error& e ){
					Jde::Post( [this, error=Exception::From(std::move(e))]()mutable{ base::ResumeExp( std::move(*error) ); } );
				}
			});
		}
	private:
		absl::AnyInvocable<T()> _work;
	};
}
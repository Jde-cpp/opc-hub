#pragma once
#include "ExecutorAwait.h"

namespace Jde::Opc::Gateway{
	//The blocking-work pool (#353):  the threads a client's build and connect run on - the slug's certificate issued under
	//Crypto::EnsureKeyCertificate's lock, trustedCertDirs scanned, the host resolved and probed for up to three seconds
	//(UAClient::ReachableUrl) - which used to run on the executor thread that awaited the connect, the same threads that run every
	//client strand and every HTTP and WebSocket handler, so a few connects to a host with slow DNS or no route stalled them all.
	//ConnectAwait and TestConnectAwait run here;  discovery (docs/clients/spec.md *Discovery*) joins them.  /workers/blocking/threads
	//of them, two by default, started on the first job.  A job answers its awaiter by posting to the executor, so the thread is free
	//for the next job the moment the blocking work is through.
	namespace BlockingPool{
		α Post( absl::AnyInvocable<void()> work )ι->void;//runs `work` on a pool thread - inline, on the caller's, once Stop has run.
		//`work` on a pool thread, refused once the process is shutting down:  a client built then would outlive UAClient::Shutdown's
		//snapshot of the registry and nothing would stop it (the reconnect chains had the same gap, subscription-disconnect #3).  What
		//it throws, the refusal included, goes to `onError` on the executor.
		α Run( absl::AnyInvocable<void()> work, absl::AnyInvocable<void(up<Exception>)> onError, SL sl )ι->void;
		//Shutdown's, before UAClient::Shutdown:  what is queued runs and the threads are joined, so no client is built after the registry's
		//snapshot - on terminate too.  The join is bounded:  each lookup a job makes has a 3 s deadline (#353 review #3, #8).
		α Stop()ι->void;
	}

	//`work` on the blocking pool, and the awaiter resumed on the executor with what it returned, or threw - as after any other
	//await, so nothing downstream runs on a pool thread.
	template<class T=void> using BlockingAwait = RunAwait<T, BlockingPool::Run>;
}
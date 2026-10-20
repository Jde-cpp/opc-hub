#pragma once
#include <open62541/client.h>
#include <jde/fwk/co/Timer.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Logger.h>

namespace Jde::Opc::Gateway{
	constexpr RequestId PingRequestId = 0;
	constexpr RequestId ConnectRequestId = std::numeric_limits<RequestId>::max();
	constexpr RequestId SubscriptionRequestId = ConnectRequestId - 1;
	extern Duration _pingInterval;//the keep-alive's period, /gateway/pingInterval:  set once by Configure, before the web server starts.
	extern Duration _ttl;//an idle client's lifetime, /gateway/ttl:  likewise.
	struct UAClient;

	//Serializes all UA_Client access for one client: open62541 clients are not thread-safe, so every UA_Client_* call
	//(run_iterate here, submissions/sync services via UAClient::PostUA) runs on _strand. Process/Clear/Stop are
	//strand-only - cross-thread callers go through UAClient, which dispatches onto the strand with a keep-alive.
	struct AsyncRequest final{
		AsyncRequest()ι;
		α Process( RequestId requestId, sv what, bool keepAlive=false )ι->void;//strand-only.  keepAlive:  not user traffic - it leaves _lastRequest, and so the idle TTL, alone.
		α Clear( RequestId requestId )ι->void;//strand-only
		α RequestDrain()ι->void{ _drainNeeded = true; }//strand-only (StateCallback inside run_iterate): the session just activated and open62541 is about to fire its own namespace-array read - hold ProcessingLoop open until OnServiceBegin sees it serviced.
		α OnServiceBegin( RequestId requestId )ι->void;//strand-only (ServiceNotificationCallback inside run_iterate): open62541 started processing a response; an id we never sent is its own traffic - the drain's wait is over.
		α SetClient( sp<UAClient> client )ι->void;//pre-concurrency (UAClient::Connect, before the first Process)
		α Stop()ι->void;//strand-only.  Ends the loop and posts FailPending - see there.
		α IsRunning()Ι->bool{ return _running.test(); }
		α IsStopped()Ι->bool{ return _stopped.test(); }
		α Strand()Ι->const boost::asio::strand<boost::asio::io_context::executor_type>&{ return _strand; }
	private:
		α UAHandle()ι->Handle;
		α ProcessingLoop()ι->DurationTimer::Task;
		α Ping( sp<UAClient> client )ι->DurationTimer::Task;
		α CancelPing()ι->void;
		α FailPending( sp<UAClient> client )ι->void;//strand-only, posted by Stop so it never runs inside a UA_Client callback:  fails every request still pending with open62541 so its awaitable's coroutine can finish and drop its sp<UAClient>.
		boost::asio::strand<boost::asio::io_context::executor_type> _strand;
		flat_set<RequestId> _requests;//strand-confined; survives Stop() until FailPending has fired the callbacks that Clear() them.
		sp<UAClient> _client;//set pre-concurrency, nulled on the strand by Stop
		Handle _handle{};//the client's, kept past Stop for the log lines FailPending's callbacks still write.
		TimePoint _lastRequest{};//strand-confined; per-client so one client's traffic doesn't mask another's TTL.
		optional<DurationTimer> _pingTimer;//strand-confined
		bool _drainNeeded{};//strand-confined; set by RequestDrain, cleared by OnServiceBegin or ProcessingLoop's give-up cap.
		bool _stopping{};//strand-confined; set by ProcessingLoop's TTL branch: ShutdownIdle is in flight but its delete-subscription pump still needs Process/the loop, so only the ping starter keys off this - Stop() ends everything else.
		atomic_flag _running;
		atomic_flag _stopped; //set at shutdown
		constexpr static ELogTags _tags{ (ELogTags)EOpcLogTags::ProcessingLoop };
	};
}

#pragma once
#include <absl/container/flat_hash_map.h>
#include <open62541/server.h>
#include <jde/opc/usings.h>

namespace Jde::Opc::Gateway{
	//The method calls a driver row's server holds for its driver's answer (docs/open-protocol/spec.md *Commands*), whatever the
	//kind.  A method's callback runs on the server's thread under the service lock:  it holds the call here, under the outputs
	//open62541 handed it, the pointer that keys the operation, hands the request to its driver's own thread and answers
	//Good_CompletesAsynchronously.  open62541 then holds the client's Call until the driver answers or the kind's call timeout
	//passes (UASettings::AsyncTimeout), when it answers Bad_Timeout and cancels the operation, which this hears (Cancelled) and
	//forgets the call by.  The answer is set on the server's own thread, the one open62541 cancels and frees operations on, so
	//no cancel comes between the look for the call and the set;  one that finds its call cancelled - a timeout, a client's
	//Cancel, the queue's limit, the shutdown - is logged and never set, since the address may key another operation by then,
	//which is why a call is its outputs and a number.  A session's close cancels nothing:  the call is answered, and open62541
	//drops the result where no session remains.  Every call is a line at Information, the hub user, the method with its
	//arguments and the answer:  a user driving a controller under the gateway's link.
	struct DriverCalls final : std::enable_shared_from_this<DriverCalls>{
		DriverCalls( ServerCnnctnNK slug )ι:_slug{ move(slug) }{}
		//A held call:  the operation's key, which call under that key it is, and the audit line's start.
		struct Call final{ const UA_Variant* Output; uint Id; string What; };
		//The callback's, before it answers Good_CompletesAsynchronously.  `call` is the method as it was called, `SelectPset( 7 )`.
		α Hold( const UA_Variant* output, UserPK user, sv call )ι->Call;
		α Held( const Call& call )Ι->bool;//open62541 has not cancelled it:  asked before a request is sent.
		//The driver's answer, from any thread, set and logged on the server's.  While the server's loop runs or before its
		//shutdown, as UAServer::Post:  a driver answers nothing once it has stopped.
		α Answer( UA_Server& server, Call call, UA_StatusCode result, string detail={} )ι->void;
		α Refused( UserPK user, sv call, UA_StatusCode result, sv detail )Ι->void;//a call the callback answers itself, never held:  its line.
		α Dropped( const Call& call, sv why )Ι->void;//a held call that is never sent nor answered here:  its line.  open62541 has answered its client, or will.
		α Cancelled( const void* output )ι->void;//open62541's asyncOperationCancelCallback, under the service lock.
		α Size()Ι->uint;//the calls held.
	private:
		α Take( const Call& call )ι->bool;//the call forgotten, where open62541 has not cancelled it.
		α What( UserPK user, sv call )Ι->string;
		const ServerCnnctnNK _slug;
		mutable absl::Mutex _mutex;
		absl::flat_hash_map<const UA_Variant*,uint> _held ABSL_GUARDED_BY(_mutex);//each call open62541 holds, by its outputs.
		uint _last ABSL_GUARDED_BY(_mutex){};
	};
}
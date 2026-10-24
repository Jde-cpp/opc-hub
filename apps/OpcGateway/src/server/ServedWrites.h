#pragma once
#include <deque>
#include <absl/container/flat_hash_map.h>
#include <absl/functional/any_invocable.h>
#include <absl/synchronization/mutex.h>
#include <open62541/server.h>
#include <jde/fwk/co/Task.h>
#include <jde/opc/uatypes/Value.h>
#include "../hist/HistRows.h"

namespace Jde::Opc::Gateway{
	struct ServedValues;
	//The held write (docs/servers/spec.md *Writes*, plan Phase 5):  a client's Write of a served variable goes to its source under the
	//collector's credential, and the client's answer is the source's.  The variable's value source (GatewayServer::Source) queues the
	//value here, on the server's thread under the service lock, and answers Good_CompletesAsynchronously;  the forward runs on the
	//executor and the collector's strand - the collector's client, connected if it isn't (ConnectAwait), and a WriteAwait of the whole
	//DataValue, status and timestamps as the client gave them, which the source gates again - and the answer is set on the endpoint's
	//thread (spec *Threading*) with UA_Server_setAsyncWriteResult, keyed by the operation's value pointer.  open62541 holds the client's
	//Write until then, or writeTimeout, when it answers Bad_Timeout and cancels the operation, which this hears (Cancelled):  the late
	//answer is logged and never set, since the address keys another operation next.  One forward at a time per node, in the order the
	//writes came;  coalescing is the spec's *Later*.  Each is logged at Information with the hub user, the node and the source's
	//answer:  a user driving a PLC under the gateway's credential.  The served value never changes here:  it changes when the source
	//reports the new value through the collector's subscription.  Its lock is its own, held for a queue step and never across a
	//UA_Server_* call, a client's or a host's.
	struct ServedWrites final : std::enable_shared_from_this<ServedWrites>{
		using Poster = function<void(absl::AnyInvocable<void(UA_Server&)>)>;//`f` on the endpoint's thread with its server:  GatewayServer::Serial's.
		ServedWrites( sp<ServedValues> values, Poster post )ι;
		//The value source's write, under the service lock, past open62541's own gate - the source's Write bit in the AccessLevel and the
		//session's in its UserAccessLevel, Update on the group's enforced row (GatewayNodeAccess::UserRights):  Good_CompletesAsynchronously
		//once the value is queued, else the refusal - Bad_WriteNotSupported for an IndexRange, since the forward carries a whole value;
		//Bad_NodeIdInvalid for an index the endpoint can't take back to the source's (ServedValues::Unmap);  Bad_NodeIdUnknown for a node
		//that isn't served.  `value` is the operation's, the key its result is set by;  `user` the session's hub user, the audit line's.
		α Write( const UA_NodeId& node, UserPK user, const UA_NumericRange* range, const UA_DataValue& value )ι->UA_StatusCode;
		α Cancelled( const void* value )ι->void;//open62541 cancelled the operation holding `value` - a timeout, a shutdown:  its answer isn't set.
	private:
		//A write as it came:  the operation's value pointer, null once cancelled;  the value, its indices the source's;  and the member
		//it goes to, resolved as it came, which a node served again for another group meanwhile doesn't move.
		struct Queued final{ const UA_DataValue* Key; Value Written; UserPK User; HistGroupPK Group; HistGroupNodePK Index; ServerCnnctnNK Slug; };
		α Forward( NodeId node )ι->VoidTask;//the front of the node's queue, on the executor;  Answer runs the next.
		α Answer( NodeId node, UA_StatusCode result, string detail )ι->void;
		const sp<ServedValues> _values;
		const Poster _post;
		mutable absl::Mutex _mutex;
		absl::flat_hash_map<NodeId,std::deque<Queued>,NodeHash,NodeEqual> _nodes ABSL_GUARDED_BY(_mutex);//each node's writes, the front in flight.
	};
}
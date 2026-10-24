#pragma once
#include <deque>
#include <absl/container/node_hash_map.h>
#include <open62541/server.h>
#include <jde/opc/uatypes/Value.h>
#include "../hist/HistRows.h"

namespace Jde::Opc::Gateway{
	struct ServedValues;
	//The held write (docs/servers/spec.md *Writes*, plan Phase 5):  a client's Write of a served variable goes to its source under the
	//collector's credential, and the client's answer is the source's.  The variable's value source (GatewayServer::Source) queues the
	//value here, on the server's thread under the service lock, and answers Good_CompletesAsynchronously;  the forward runs on the
	//executor and the collector's strand - the collector's client, connected if it isn't (ConnectAwait), and a WriteAwait of the whole
	//DataValue, status and timestamps as the client gave them, which the source gates again - and the answer is set on the server's own
	//thread with UA_Server_setAsyncWriteResult, keyed by the operation's value pointer:  a timed callback (Answered), on the thread
	//open62541 cancels and frees operations on, so none comes between the key's read and the set (spec *Writes*).  open62541 holds the client's
	//Write until then, or writeTimeout, when it answers Bad_Timeout and cancels the operation, which this hears (Cancelled):  the late
	//answer is logged and never set, since the address keys another operation next, and a write cancelled before it is sent - behind
	//a slow forward, past the queue's limit, by a client's Cancel - is dropped, never sent.  One forward at a time per node, in the
	//order the writes came;  coalescing is the spec's *Later*.  Each is logged at Information with the hub user, the node and the
	//source's answer:  a user driving a PLC under the gateway's credential.  The served value never changes here:  it changes when the source
	//reports the new value through the collector's subscription.  Its lock is its own, held for a queue step and never across a
	//UA_Server_* call, a client's or a host's.
	struct ServedWrites final : std::enable_shared_from_this<ServedWrites>{
		ServedWrites( sp<ServedValues> values )ι:_values{ move(values) }{}
		α Start( UA_Server& server )ι->void;//the endpoint's server, made:  answers are set on its thread from here until Stop.
		//The value source's write, under the service lock, past open62541's own gate - the source's Write bit in the AccessLevel and the
		//session's in its UserAccessLevel, Update on the group's enforced row (GatewayNodeAccess::UserRights):  Good_CompletesAsynchronously
		//once the value is queued, else the refusal - Bad_WriteNotSupported for an IndexRange, since the forward carries a whole value;
		//Bad_NodeIdInvalid for an index the endpoint can't take back to the source's (ServedValues::Served);  Bad_NodeIdUnknown for a node
		//that isn't served;  Bad_Shutdown once stopped.  `value` is the operation's, the key its result is set by;  `user` the session's hub user, the audit line's.
		α Write( const UA_NodeId& node, UserPK user, const UA_NumericRange* range, const UA_DataValue& value )ι->UA_StatusCode;
		//The endpoint is stopping, before its server goes:  no write is queued or sent from here on, and an answer is logged and never
		//set, those waiting for the server's thread included.  Once it returns, nothing here calls into the server.
		α Stop()ι->void;
		α Cancelled( const void* value )ι->void;//open62541 cancelled the operation holding `value` - a timeout, the queue's limit, a client's Cancel, a shutdown:  its answer isn't set, nor the write sent if it wasn't.
	private:
		//A write as it came:  the operation's value pointer, null once cancelled;  the value, its indices the source's, moved out by the
		//forward, its one reader;  and the member it goes to, resolved as it came, which a node served again for another group
		//meanwhile doesn't move.
		struct Queued final{ const UA_DataValue* Key; Value Written; UserPK User; HistGroupPK Group; HistGroupNodePK Index; ServerCnnctnNK Slug; };
		struct Ready final{ NodeId Node; UA_StatusCode Result; string Detail; };//an answer waiting for the server's thread.
		α Forward( NodeId node )ι->VoidTask;//the front of the node's queue, on the executor;  Answer runs the next.
		α Answer( NodeId node, UA_StatusCode result, string detail )ι->void;
		α Schedule()ι->void;//Answered on the server's thread, which is woken for it.
		Ω Answered( UA_Server* server, void* self )ι->void;//the timed callback:  every ready answer set.
		α SetReady( UA_Server* ua, sv unset )ι->void;
		//An answer:  the front popped, the result set and the audit line, then the node's next write forwarded.  On the server's
		//thread;  without a server, never set, `unset` saying why.
		α Set( UA_Server* ua, const NodeId& node, UA_StatusCode result, sv detail, sv unset )ι->void;
		α Drop( const NodeId& node, bool take=false )ι->optional<Queued>;//the cancelled writes off the queue's front:  the front left, its value moved out with `take`;  none once it's empty.
		α Live( const NodeId& node )Ι->bool;//the queue's front isn't cancelled, nor the writes stopped.
		const sp<ServedValues> _values;
		mutable absl::Mutex _mutex;
		//Each node's writes, the front in flight.  A node map, and a deque keeps its elements in place as it grows at the back and
		//shrinks at the front, so _keys' pointers hold until their write is popped.
		absl::node_hash_map<NodeId,std::deque<Queued>,NodeHash,NodeEqual> _nodes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<const UA_DataValue*,Queued*> _keys ABSL_GUARDED_BY(_mutex);//each queued write not cancelled, by its key:  Cancelled's lookup.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Pop( std::deque<Queued>& queue )ι->void;//the front, and its key from _keys.
		vector<Ready> _ready ABSL_GUARDED_BY(_mutex);//the answers for the server's thread, one timed callback for them all.
		bool _stopped ABSL_GUARDED_BY(_mutex){};
		absl::Mutex _serverMutex;//held across a schedule, so Stop waits for one under way before the server can go.
		UA_Server* _server ABSL_GUARDED_BY(_serverMutex){};
	};
}
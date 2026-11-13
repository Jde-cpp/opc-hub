#pragma once
#include "Trace.h"

//The engine (docs/flows/spec.md *Messages*, *Errors and status*):  one flow's graph run on a strand of its own, with no
//gateway in it.
namespace Jde::Opc::Flows{
	α ToBytes( sv text )ι->optional<uint>;//a size as the settings write it:  "16MiB", "256KiB", "2GiB" or the bytes, "4096".

	//Each node has an input queue bounded at `Queue`, past which its oldest message is dropped, counted and traced.  The
	//strand drains the queues in arrival order, one message through one node at a time;  a node that waits holds its message
	//and comes back to the strand, so no two nodes of a flow run at once.  Every timer is the clock's, so a flow under a
	//manual clock, or a stream's, takes no wall-clock time.  A flow runs once:  a restart is another Flow.
	struct Flow final{
		struct Config final{
			string Name;//for the log.
			sp<Hist::IClock> Clock;//null:  SystemClock().
			sp<IHost> Host;//null:  a graph with a hosted node does not start.
			sp<ITrace> Trace;//null:  nothing is traced, and the counts are still kept.
			sp<MessageIds> Ids;//the gateway's;  null:  the flow's own, from its clock.
			sp<GlobalStore> Global;//the gateway's;  null:  the flow's own.
			uint Queue{ 1000 };
			FunctionLimits Function;
			jobject Env;//what a body's `env.get` answers by name, beside the node's and the flow's own.
			jobject Context;//the flow scope's values, as loaded.
			flat_map<string,jobject> NodeContext;//each node scope's, by node id.
			function<void( sv nodeId, sv key, const jvalue* value )> ContextChanged;//a flow or node scope's change, on the strand:  no node id for the flow's, a null value for a key that went.
		};
		Flow( Graph graph, Config config )ι;
		~Flow();//stops it.
		Flow( Flow&& )ι=default;
		α operator=( Flow&& rhs )ι->Flow&;//stops the flow it replaces.

		//Makes the nodes, the host's among them, and posts their Start.  False when one could not be made, or the flow has
		//run already:  it stays stopped, with Fault and the node's status saying why.  False too, with no fault, for a Stop
		//while the nodes were made.  A node whose Start throws stops the flow the same way, after this has returned.
		//`started`:  on the strand once the nodes' Start has run, the flow running or stopped by one that threw;  here, when
		//this answers false.
		α Start( absl::AnyInvocable<void()> started={} )ι->bool;
		//Nothing more is queued or taken.  On the strand the timers are cancelled, each node's Stop is called and what was
		//queued or held is dropped and counted;  `stopped` runs after, there.  For a flow that has stopped already it runs there
		//too, behind the teardown, and for one that never started it runs here.
		α Stop( absl::AnyInvocable<void()> stopped={} )ι->void;
		//Stopped by its host for a node, whose admission went:  as Stop, with `why` as its Fault and the node's status saying
		//it, as a refusal at Start leaves them.  For a flow that never started too;  one that has stopped keeps the fault it has.
		α Stop( Flows::Fault why, absl::AnyInvocable<void()> stopped={} )ι->void;
		α Running()Ι->bool;
		α Fault()Ι->optional<Flows::Fault>;
		//An inject node's message, `message` in place of its props' when given, `payload` its time when it has none.  Throws
		//for a node that is not an inject of this flow and for a flow that is not running.
		α Inject( sv nodeId, const jobject* message=nullptr )ε->void;
		//Another flow's message at this flow's `link in` of the name, from any thread:  a clone under a new `_id`, `m`'s its
		//`_parent`, born at the `link in`.  False for a flow that is not running, and for one with no `link in` of the name.
		α Link( sv name, const Message& m )ι->bool;
		//A context value from outside the flow, the Context tab's:  set on the strand, as a node's own is, so the host is told
		//of it.  `nodeId`:  none for the flow's scope;  a null `value` takes the key away.  `set`:  whether it was, there, or
		//false, here, for a flow that is not running and for a node that is not its.
		α SetContext( sv nodeId, sv key, jvalue value, absl::AnyInvocable<void( bool )> set={} )ι->void;
		α Post( absl::AnyInvocable<void()> f )ι->void;//`f` on the strand, from any thread;  dropped unless the flow is running.
		α Status( sv nodeId )Ι->NodeStatus;
		α Counts()Ι->vector<NodeCounts>;//since the start, a row per node in the graph's order.
		//The minutes that have ended and were not taken, oldest first, each with the nodes that had a figure in it;  at most an
		//hour of them is kept.  A minute ends on the flow's clock.
		α TakeMinutes()ι->vector<MinuteCounts>;
	private:
		sp<Engine> _engine;
	};
}
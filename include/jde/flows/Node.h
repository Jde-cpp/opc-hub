#pragma once
#include <jde/historian/Clock.h>
#include "Context.h"
#include "Graph.h"
#include "Message.h"

//What a flow runs (docs/flows/spec.md *Messages*, *Nodes*):  the node interface the library's own nodes and a host's
//sources and sinks implement.
namespace Jde::Opc::Flows{
	struct Engine; struct Slot;

	//Node-RED's `{ fill, shape, text }`:  red, green, yellow, blue or grey, a ring or a dot.
	struct NodeStatus final{
		string Fill;
		string Shape;
		string Text;
		bool operator==( const NodeStatus& )const = default;
	};

	//A node of a running flow.  The flow calls Start, Receive and Stop on its strand, one at a time, and gives the node its
	//way out through the members below, which are for the strand too but where they say otherwise;  none is for a constructor,
	//which runs before the flow has taken the node.
	struct INode{
		using TimerId = uint;
		virtual ~INode()=default;
	protected:
		//Before the flow's first message.  What it throws is the node's refusal:  the flow stops with it as its fault.
		β Start()ε->void{}
		//After a Start that returned:  nothing more comes, the node's timers are cancelled, and what it holds is counted as dropped.
		β Stop()ι->void{}
		//A message off the node's input queue, the node's to change and to send on.  It is finished when this returns unless
		//the node holds it, and what this throws is the node's error with it.
		β Receive( Message& m )ε->void=0;

		α Def()Ι->const NodeDef&;
		α Now()Ι->TimePoint;//the flow's clock.  Any thread.
		//A message made here, with its `_id`, its `_time` and `parent` as its `_parent`.  Any thread.
		α Make( MessageId parent=0 )ι->Message;
		α Clone( const Message& m )ι->Message;//`m` under a new `_id`, `m`'s its `_parent`.  Any thread.
		//Down the port's wires:  the message itself down the first and a clone down each other.  A message is the flow's, what
		//Receive handed over or Make or Clone made, and is sent once.
		α Send( Message&& m, uint port=0 )ι->void;
		α SendEach( Message&& m, std::span<const uint> ports )ι->void;//the message received, down the first port and a clone down each other;  no port, and it ends here.
		α Hold()ι->void;//in Receive:  the message stays the node's past its return, until Done, Fail or Drop.
		α Done( MessageId id )ι->void;//a held message is finished.
		α Fail( Message&& m, string error )ι->void;//the node's error with a held message:  the catch nodes take it.
		α Drop( MessageId id )ι->void;//a held message the node gives up, counted and traced as a queue's are.
		α SetStatus( NodeStatus status )ι->void;
		//`f` on the strand, once, on the flow's clock:  never after Stop.  0 once the flow has stopped.
		α Schedule( Duration after, absl::AnyInvocable<void()> f )ι->TimerId;
		α Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId;
		α Cancel( TimerId id )ι->bool;//true when `f` had not run and now never will.
		//`f` on the strand, from any thread:  how what a node waited for off the strand comes back.  Dropped once the flow has stopped.
		α Post( absl::AnyInvocable<void()> f )ι->void;
		α Context()ι->Store&;//the node's scope.
		α FlowContext()ι->Store&;
		α Global()ι->GlobalStore&;//any thread.
		α QueueLimit()Ι->uint;//the flow's `queue`, for what a node buffers of its own.
	private:
		Slot* _slot{};//the flow's side, set when it takes the node.
		friend struct Engine;
		friend struct ISource;
	};

	//A source a host makes:  it takes no message.
	struct ISource : INode{
	protected:
		//From any thread:  queued at the node, under the bound every queue has, and sent down its output on the strand.
		α Emit( Message&& m )ι->void;
		α Receive( Message& )ε->void final{}
	};
	//A sink a host makes:  Receive is its work, and a node with an output sends what it answers.
	struct ISink : INode{};

	//What makes the hosted types' nodes (IsHosted) as a flow starts, and as a sink a `link out` whose name no `link in` of
	//its flow holds, another flow's.  What it throws is why the node cannot be, a `group in` on a gateway with no historian:
	//the flow stays stopped with it as its fault.
	struct IHost{
		virtual ~IHost()=default;
		β Source( const NodeDef& node )ε->up<ISource> =0;
		β Sink( const NodeDef& node )ε->up<ISink> =0;
	};
}
#pragma once
#include <jde/flows/Replay.h>
#include "FlowRows.h"
#include "../hist/HistRows.h"

namespace Jde::Opc::Gateway{
	struct FlowHost; struct HistGroupHost;
	//What a run's hosted nodes are made from:  the flow, its deployer, whose rights a link's delivery asks, and each
	//`group in` node's group, the group that holds each `opc in`'s node, and an index-form node's spelling (HistNodeText),
	//as the run's admission found them, by the node's id.
	struct FlowRun final{ FlowPK Flow; string Slug; UserPK Deployer; flat_map<string,HistGroupPK> Groups; flat_map<string,string> Nodes; };
	//A replay's range, each bound inside it, whether it commits, and the most records a page of a source's holds,
	//`/gateway/flows/readLimit`, 0 for the historian's own.
	struct FlowReplay final{ TimePoint From; TimePoint To; bool Commit; uint ReadLimit; };

	//A committed replay's writes (docs/flows/spec.md *Replay*):  the values its `variable` sinks take, each into the history
	//of the group that holds the variable, through the group's edit as Update, which inserts or replaces, by the flow's
	//deployer, whose Update on the group the run's start asked.  The range is cleared as the values go:  each edit deletes
	//a variable's history from where its last left off through its own last time, then writes its values, and a run that
	//completes, no value failed by then, clears each variable on through `to`, so the derived history is the run's, and a
	//run that stops partway leaves its own values up to there and what was stored after.  One edit is out at a time, the groups served in turn,
	//and what comes meanwhile goes in the next, so a run of thousands of values is a few flushes.  A value outside the
	//range, one its edit refuses, and each of an edit that throws, a group removed meanwhile, is counted, as is a delete
	//refused, the first's reason kept:  the run's end waits for the last edit and fails by them.
	struct FlowCommits final : std::enable_shared_from_this<FlowCommits>{
		FlowCommits( Opc::Hist::Writer by, TimePoint from, TimePoint to )ι;
		α Hold( sp<HistGroupHost> group, Opc::Hist::NodeIndex node, sv variable )ι->void;//a variable the run writes, whether or not it is sent a value.
		α Add( sp<HistGroupHost> group, Opc::Hist::NodeIndex node, sv variable, Value&& value )ι->void;
		α Complete()ι->void;//the run completed:  each variable's history cleared on through `to`, unless a value or a delete has failed.
		α Waiting()Ι->uint{ lg _{ _mutex }; return _waiting; }//the values no edit has answered for.
		α Drained( absl::AnyInvocable<void()> f )ι->void;//`f` once nothing waits, the last delete included:  here, when nothing does.
		α Eased( absl::AnyInvocable<void()> f )ι->void;//`f` once no more than half of Buffer waits:  here, when that is so.
		struct Totals final{ uint Written{}; uint Failed{}; uint Uncleared{}; string First; };//Uncleared:  the deletes refused.
		α Result()Ι->Totals{ lg _{ _mutex }; return _totals; }
		static constexpr uint Buffer{ 10'000 };//the values a run lets wait before it stops for their edit.
	private:
		//Through:  where a completed run's delete ends, 0 for none;  Clear:  the range Send found its delete takes.
		struct Node final{ string Variable; vector<Value> Values; UA_DateTime Through{}; optional<std::pair<UA_DateTime,UA_DateTime>> Clear; };
		struct Batch final{ sp<HistGroupHost> Group; flat_map<Opc::Hist::NodeIndex,Node> Nodes; uint Waiting{}; };//Waiting:  what it counts in _waiting.
		//What each variable's history is the run's through, by its group and member:  where the next delete starts after.
		struct Held final{ sp<HistGroupHost> Group; string Variable; UA_DateTime Cleared; };
		α Fail( uint count, sv variable, sv why )ι->void;//under _mutex.
		α Send()ι->optional<Batch>;//under _mutex:  the next group's batch after the last served, when no edit is out.
		α Start( optional<Batch> batch )ι->void;//its edit, posted:  outside _mutex, as the group may refuse it inline.
		Ω Editing( sp<FlowCommits> self, Batch batch )ι->VoidTask;
		α Tell()ι->vector<absl::AnyInvocable<void()>>;//under _mutex:  the waits that are now over.
		const Opc::Hist::Writer _by;
		const UA_DateTime _from, _to;
		mutable std::mutex _mutex;
		flat_map<HistGroupPK,Batch> _pending;
		flat_map<std::pair<HistGroupPK,Opc::Hist::NodeIndex>,Held> _held;
		optional<HistGroupPK> _served;//the group whose batch went last.
		bool _out{};
		uint _waiting{};//values, and the deletes of a completed run.
		Totals _totals;
		vector<absl::AnyInvocable<void()>> _drained, _eased;
	};

	//A run's sources and sinks (docs/flows/spec.md *Sources*, *Sinks*), each on the connection's collector client where it
	//has a connection, so under the gateway's own credential:
	//  `opc in`:  a listener of its own on the node's item, with the sampling, queue, discard and filter its props ask,
	//  merged with what the item's other listeners ask;  each value a message, the loss one with Bad_NoCommunication.
	//  `group in`:  the group's records from a time, a page at a time as the flow finishes them, then as they are stored,
	//  from its mark less `lateness` at a restart, a record at or before the last it finished of that node left out.
	//  `timer`:  `every`, at the multiples of the duration from the epoch, or `at`, local times on weekdays in a zone.
	//  `calendar`:  a calendar's shift and break boundaries, from the client's snapshot.
	//  `connection`:  a connection's loss and return, from the registry's changes.
	//  `opc out`:  a write of the message's value toward the node's DataType, one at a time, the message held for the answer.
	//  `log`:  a line under the Flow tag.
	//  `http request`:  to a host /gateway/flows/http/allow holds, each redirect's host checked as the first was, MaxRequests
	//  at once.
	//  `link out` to another flow:  a clone to each running flow that holds the `link in`, under the deployer's Update on it.
	//  `variable`:  the message's `payload` read toward the variable's type, with the message's `status` or Good, its
	//  `sourceTime` or `_time` as the source time and the flow's clock's now as the server's, pushed to the flow server
	//  (FlowServer.h), which a flow without one does not start.
	//A replay's (spec *Replay*, Flows::IReplayHost), made with its range:  a `group in`'s records, and those of the group that
	//holds an `opc in`'s node, found by the node as its admission spelled it, read from the group a page a call over the range, each a message as the node sends it
	//live, a heartbeat left out and no modification a message of its own;  a `timer` and a `calendar` as they are, on the
	//run's clock;  and every sink a capture, the value an `opc out` would have written, a `log`'s line, an `http request`'s
	//method, url and body, its host asked of the allow list as at a send, the message a link would have delivered and the
	//value a `variable` would have pushed:  nothing leaves the gateway.  A committed run's `variable` writes that value as
	//well, into the history of the group that holds the variable (FlowCommits), which the run's `Groups` names by the node.
	struct FlowNodes final : Flows::IReplayHost{
		FlowNodes( FlowHost& host, FlowRun run, optional<FlowReplay> replay={} )ι;
		α Source( const Flows::NodeDef& node )ε->up<Flows::ISource> override;
		α Sink( const Flows::NodeDef& node )ε->up<Flows::ISink> override;
		α Stream( const Flows::NodeDef& node )ε->up<Flows::IStream> override;
		//A deploy's:  a hosted node's props read as its node reads them, and what else a deploy alone refuses, a calendar
		//the gateway does not hold and a literal url whose host the allow list lacks.  Throws the node's fault.
		Ω Check( const Flows::NodeDef& node, SRCE )ε->void;
		α Commits()Ι->const sp<FlowCommits>&{ return _commits; }//a committed replay's;  null for any other.
		//A group's members changed (FlowHost::GroupChanged):  each `group in` on it that takes every member subscribes again.
		Ω GroupChanged( HistGroupPK group )ι->void;
		static constexpr Duration RetryInterval{ 30s };//how soon a subscription that failed is asked for again.
		static constexpr uint MaxRedirects{ 5 };
		static constexpr uint MaxRequests{ 8 };//an `http request` node's out at once.
	private:
		FlowHost& _host;
		const FlowRun _run;
		const optional<FlowReplay> _replay;
		sp<FlowCommits> _commits;
	};
}
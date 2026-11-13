#pragma once
#include <jde/flows/Node.h>
#include "FlowRows.h"
#include "../hist/HistRows.h"

namespace Jde::Opc::Gateway{
	struct FlowHost;
	//What a run's hosted nodes are made from:  the flow, its deployer, whose rights a link's delivery asks, and each
	//`group in` node's group as the run's admission found it, by the node's id.
	struct FlowRun final{ FlowPK Flow; string Slug; UserPK Deployer; flat_map<string,HistGroupPK> Groups; };

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
	//A `variable` waits for the flow server, and is refused.
	struct FlowNodes final : Flows::IHost{
		FlowNodes( FlowHost& host, FlowRun run )ι:_host{ host }, _run{ move(run) }{}
		α Source( const Flows::NodeDef& node )ε->up<Flows::ISource> override;
		α Sink( const Flows::NodeDef& node )ε->up<Flows::ISink> override;
		//A deploy's:  a hosted node's props read as its node reads them, and what else a deploy alone refuses, a `variable`,
		//a calendar the gateway does not hold and a literal url whose host the allow list lacks.  Throws the node's fault.
		Ω Check( const Flows::NodeDef& node, SRCE )ε->void;
		//A group's members changed (FlowHost::GroupChanged):  each `group in` on it that takes every member subscribes again.
		Ω GroupChanged( HistGroupPK group )ι->void;
		static constexpr Duration RetryInterval{ 30s };//how soon a subscription that failed is asked for again.
		static constexpr uint MaxRedirects{ 5 };
		static constexpr uint MaxRequests{ 8 };//an `http request` node's out at once.
	private:
		FlowHost& _host;
		const FlowRun _run;
	};
}
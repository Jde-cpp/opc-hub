#pragma once
#include <jde/historian/Historian.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Server{
	//The host's side of the history backend (docs/servers/spec.md *The server library*):  whose historian the server serves,
	//which group keeps each node's history, which properties carry the archive's start, and whether open62541's setValue
	//stores.  UAHistory keeps no group or index map of its own and asks these under open62541's service lock, but for the
	//archive starts, which the midnight timer asks on the executor:  a lookup each, never a wait or a throw.  The
	//OpcServer's is OpcServerHistoryHost, one group of what its nodesets mark Historizing;  the gateway's answers from its
	//served nodes' groups, and its collector stores, not setValue (plan, Phase 4).
	struct IHistoryHost{
		virtual ~IHistoryHost()=default;
		//The historian whose groups the server serves, null for a server that keeps no history:  then no backend is
		//installed.  Read when the server is configured, so it is set before the server is made and kept while it runs.
		β Historian()Ι->sp<Hist::Historian> = 0;
		//Whether the values written to the server's variables are stored (UAHistory::Collect):  the OpcServer's are written
		//by its clients and its PubSub reader, where the gateway's served values are the collector's, stored as they arrive.
		β Collects()Ι->bool=0;
		//A node as the host keeps its history:  the group that keeps it and its index there.  Group is never null, here or
		//in an ArchiveStart:  UAHistory uses it unchecked.
		struct Node final{ sp<Hist::Group> Group; Hist::NodeIndex Index; };
		β Find( const UA_NodeId& node )Ι->optional<Node> = 0;//none for a node that isn't historized.
		//Each HA Configuration property that holds the archive's start, StartOfArchive and StartOfOnlineArchive, with the
		//group whose earliest day it takes:  written at UAHistory::Start and after each midnight.
		struct ArchiveStart final{ NodeId Property; sp<Hist::Group> Group; };
		β ArchiveStarts()Ι->vector<ArchiveStart> = 0;
	};
}
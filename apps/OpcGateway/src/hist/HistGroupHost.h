#pragma once
#include <absl/container/flat_hash_map.h>
#include <jde/historian/Historian.h>
#include "../auth/OpcServerSession.h"
#include "../types/MonitoringNodes.h"
#include "HistRows.h"

namespace Jde::Opc::Gateway{
	//A member the library already holds, as the group's rows had it at start.
	struct HistMember final{ HistGroupNodePK Index; ExNodeId Node; Opc::Hist::Thresholds Thresholds; };

	//One hist_groups row as the gateway hosts it (spec *Collection*, *Collector contract*):  the library's group, the
	//collector's monitored items on the group's connection, and the listener that joins the two.  Membership and thresholds
	//change here as HistGroupMAwait applies the tables' edits; the items follow on the collector's own client, asked for again
	//every RetryInterval until the server takes them.  Values arrive at SendDataChange, which 5c (#224) feeds to the group.
	struct HistGroupHost final : IDataChange, std::enable_shared_from_this<HistGroupHost>{
		HistGroupHost( const HistGroupRow& row, ServerCnnctnNK slug, sp<Opc::Hist::Group> group, vector<HistMember> members, Credential collector, Opc::Hist::IClock& clock )ι;
		~HistGroupHost();
		α Id()Ι->HistGroupPK{ return _id; }
		α Guid()Ι->str{ return _guid; }
		α Slug()Ι->const ServerCnnctnNK&{ return _slug; }
		α Group()Ι->const sp<Opc::Hist::Group>&{ return _group; }
		α Columns()Ι->HistThresholdColumns;//the group's own threshold columns, the last fallback a member's resolution makes.
		α SetColumns( HistThresholdColumns columns )ι->void;
		//Joins the row's node to the group, its NodeAdded by `by`, and asks the server for its item.  Throws as Group::Add does.
		α Join( const HistGroupNodeRow& row, Opc::Hist::Thresholds thresholds, optional<Opc::Hist::Writer> by, SRCE )ε->void;
		//The node leaves, its NodeRemoved by `by`, and its item goes.  Throws as Group::Remove does.
		α Leave( HistGroupNodePK index, optional<Opc::Hist::Writer> by, SRCE )ε->void;
		//Nodes' thresholds re-resolved:  the library's, and in one ModifyMonitoredItems the sampling and queue of each item whose
		//parameters that changes, in place (spec *Collector contract*).  Throws as Group::SetThresholds does.
		α SetNodeThresholds( vector<std::pair<HistGroupNodePK,Opc::Hist::Thresholds>> nodes, SRCE )ε->void;
		α FindThresholds( HistGroupNodePK index )Ι->optional<Opc::Hist::Thresholds>;
		α Members()Ι->vector<std::pair<HistGroupNodePK,ExNodeId>>;
		α Subscribed()Ι->uint;//the members the server has given items.
		//Every item goes and no retry runs:  after Historian::RemoveGroup, and at the gateway's end.
		α Close()ι->void;
		//Asks the server for an item for every member that has none, on the collector's client, each node by the index its URI
		//has there.  A connect or create that fails is logged and tried again after RetryInterval, until Close.
		α Subscribe()ι->void;

		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& )ι->void override{}//5c (#224):  the collector feeding the library.
		α ConnectionLost( const ServerCnnctnNK&, const flat_set<NodeId>&, TimePoint when )ι->void override{ _group->Disconnected( when ); }
		α ConnectionRestored( const ServerCnnctnNK&, const flat_set<NodeId>& )ι->void override{ _group->Connected(); }
		α ItemParameters( const NodeId& node )Ι->MonitoringParameters override;
		α to_string()Ι->string override{ return Ƒ( "HistGroup({})", _guid ); }
		//What the collector asks of a node's item (spec *Collector contract*):  sampling at its MinTimeInterval, or at 0, the
		//server's fastest, when it has none, and a publishing interval's samples queued - the publishing interval over the
		//sampling interval, rounded up, plus one - or the deepest queue the server grants when sampling at 0.
		Ω Parameters( const Opc::Hist::Thresholds& thresholds )ι->MonitoringParameters;
		static constexpr Duration PublishingInterval{ 500ms };//the subscription's default, which SubscribeAwait keeps.
		static constexpr Duration RetryInterval{ 30s };
	private:
		//Warned:  its namespace or its item was refused, and logged as a warning once.  Stale:  its thresholds changed while a create
		//was out, which asked for the old parameters, so the pass modifies the item.
		struct Node final{ ExNodeId Ex; optional<NodeId> Id; Opc::Hist::Thresholds Thresholds; bool Subscribed{}; bool Warned{}; bool Stale{}; };
		α Subscribing()ι->VoidTask;
		α Unsubscribe( flat_set<NodeId>&& nodes )ι->void;
		α Modifying( flat_map<NodeId,MonitoringParameters> parameters )ι->VoidTask;
		α Retry()ι->void;

		const HistGroupPK _id;
		const string _guid;
		const ServerCnnctnNK _slug;
		const sp<Opc::Hist::Group> _group;
		const Credential _collector;
		Opc::Hist::IClock& _clock;
		mutable std::mutex _mutex;
		HistThresholdColumns _columns;
		flat_map<HistGroupNodePK,Node> _nodes;
		absl::flat_hash_map<NodeId,HistGroupNodePK> _byNode;//by the server's index, once a connection gave it.
		Opc::Hist::IClock::TimerId _retry{};
		bool _subscribing{}, _again{}, _closed{};//_again:  a member joined while a pass was out, which runs another.
	};
}
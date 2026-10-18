#pragma once
#include <absl/container/flat_hash_map.h>
#include <jde/historian/Historian.h>
#include "../async/NamespacesAwait.h"
#include "../auth/OpcServerSession.h"
#include "../types/MonitoringNodes.h"
#include "HistRows.h"

namespace Jde::Opc::Gateway{
	//A member the library already holds, as the group's rows had it at start.
	struct HistMember final{ HistGroupNodePK Index; ExNodeId Node; Opc::Hist::Thresholds Thresholds; };
	//What a member's server says of its node, read once its item is made (HistGroupHost::Describe):  whether its DataType is an
	//enumeration, none until the server has said, and its InstrumentRange and EURange properties, each none where it has
	//none, once Ranged.
	struct HistDescription final{ optional<bool> Enumeration; optional<Opc::Hist::Range> Instrument; optional<Opc::Hist::Range> EU; bool Ranged{}; };
	//Whether a member's ExceptionDeviation band is in force (#338):  none set, applied, or dropped - for an enumeration, or for a
	//percent format whose range the node lacks or can't use, either of which stores every change - or waiting for the server to
	//describe the node, as ForGroup waits.
	enum class EHistBand : uint8{ None, Applied, Enumeration, Waiting, NoRange };

	//One hist_groups row as the gateway hosts it (spec *Collection*, *Collector contract*):  the library's group, the
	//collector's monitored items on the group's connection, and the listener that joins the two.  Membership and thresholds
	//change here as HistGroupMAwait applies the tables' edits; the items follow on the collector's own client, asked for again
	//every RetryInterval until the server takes them.  Every value the items deliver goes to the group, as do the connection's
	//break and return.
	//
	//Each member is stored by namespace URI and monitored by the index its server has for it, so a rebuild that put the items
	//back - by index - is checked against the server's NamespaceArray before its values count:  they wait meanwhile, and a
	//server that moved a member's namespace to another index gets every item again by URI, its values through the gap rule;
	//one appended moves none.  Nor does a member stay lost
	//for good:  one the gateway lost and hasn't restored within AbandonAfter, which a refused rebuild, a rejected reconnect
	//or a removed client leaves silent, is asked for again as a new member is.
	struct HistGroupHost final : IDataChange, std::enable_shared_from_this<HistGroupHost>{
		HistGroupHost( const HistGroupRow& row, ServerCnnctnNK slug, sp<Opc::Hist::Group> group, vector<HistMember> members, Credential collector, Opc::Hist::IClock& clock )ι;
		~HistGroupHost();
		α Id()Ι->HistGroupPK{ return _id; }
		α Guid()Ι->str{ return _guid; }
		α Name()Ι->string;//the row's, as a group is shown (historyGroups).
		α SetName( string name )ι->void;
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
		α Description( HistGroupNodePK index )Ι->optional<HistDescription>;
		α Band( HistGroupNodePK index )Ι->optional<EHistBand>;//as ForGroup and the library's range check leave it;  none for a node it doesn't hold.
		//A member's node by the server's index, once its connection gave it:  how a node page keys its variables.  None before that.
		α ServerId( HistGroupNodePK index )Ι->optional<NodeId>;
		α Members()Ι->vector<std::pair<HistGroupNodePK,ExNodeId>>;
		//The member a read or an edit names:  by URI, or by the index the server had for it when the collector last read the
		//server's namespaces.  None for a node that isn't a member, or one by an index the collector hasn't seen.
		α Index( const ExNodeId& node )Ι->optional<HistGroupNodePK>;
		//Each node's, in order.  A member named twice, by two spellings, is refused, BadRequest:  the library would return its
		//values once and its bounds twice.
		α Indexes( const vector<ExNodeId>& nodes, SRCE )Ε->vector<optional<HistGroupNodePK>>;
		α Subscribed()Ι->uint;//the members the server has given items.
		//Every item goes and no retry runs:  after Historian::RemoveGroup, and at the gateway's end.
		α Close()ι->void;
		//Asks the server for an item for every member that has none, on the collector's client, each node by the index its URI
		//has there.  A connect or create that fails is logged and tried again after RetryInterval, until Close.  Also arms the
		//watch that asks again for a member lost too long.
		α Subscribe()ι->void;
		//Describes again each member whose description Describe lost an answer for, rather than was refused:  the watch's, every
		//RetryInterval.
		α Redescribe()ι->void;

		//Into the group, on the connection's strand:  Group::Enqueue only buffers.  Held while a rebuild's namespaces are checked.
		α SendDataChange( const ServerCnnctnNK&, const NodeId& node, const Value& value )ι->void override;
		α ConnectionLost( const ServerCnnctnNK&, const flat_set<NodeId>& nodes, TimePoint when )ι->void override;
		//On the strand, before the new subscription's first value:  the check of the server's namespaces starts here.
		α ConnectionRestored( const ServerCnnctnNK&, const flat_set<NodeId>& nodes )ι->void override;
		α ItemParameters( const NodeId& node )Ι->MonitoringParameters override;
		α to_string()Ι->string override{ return Ƒ( "HistGroup({})", _guid ); }
		//What the collector asks of a node's item (spec *Collector contract*):  sampling at its MinTimeInterval, or at 0, the
		//server's fastest, when it has none, and a publishing interval's samples queued - the publishing interval over the
		//sampling interval, rounded up, plus one - or the deepest queue the server grants when sampling at 0.
		Ω Parameters( const Opc::Hist::Thresholds& thresholds )ι->MonitoringParameters;
		//The thresholds the group takes from a member's resolved ones (spec *Compression*):  no ExceptionDeviation for an
		//enumeration, whose Int32 values the library can't tell from a quantity, and a percent-of-range format's band a
		//percentage of the InstrumentRange or EURange the server gives the node.  Until the server has described the node, the
		//band waits and every change is stored:  a state is never dropped as noise, nor a range warned of before it is read.
		Ω ForGroup( Opc::Hist::Thresholds thresholds, const HistDescription& described={} )ι->Opc::Hist::Thresholds;
		static constexpr Duration PublishingInterval{ 500ms };//the subscription's default, which SubscribeAwait keeps.
		static constexpr Duration RetryInterval{ 30s };
		//How long a member may stay lost before it is asked for again:  past the reconnect chain's rejections, while it still
		//retries a server that is down.
		static constexpr Duration AbandonAfter{ 2min };
	private:
		//Warned:  its namespace or its item was refused, and logged as a warning once.  Stale:  its thresholds changed while a create
		//was out, which asked for the old parameters, so the pass modifies the item.  Lost:  since when its item has been gone, a
		//ConnectionLost's or one the watch found, until a ConnectionRestored brings it back.  Fed:  a value arrived since its
		//create, which the read after joining an existing item would repeat.  Thresholds are the rows', which ForGroup applies
		//Described to.  Undescribed:  an answer Describe asked for was lost, which Redescribe asks for again.
		struct Node final{ ExNodeId Ex; optional<NodeId> Id; Opc::Hist::Thresholds Thresholds; bool Subscribed{}; bool Warned{}; bool Stale{}; optional<TimePoint> Lost; bool Fed{}; HistDescription Described; bool Undescribed{}; };
		α Subscribing()ι->VoidTask;
		α Unsubscribe( flat_set<NodeId>&& nodes )ι->void;
		α Modifying( flat_map<NodeId,MonitoringParameters> parameters )ι->VoidTask;
		α Retry()ι->void;
		//Joining an item the collector's client already had, which sends no initial value:  each node's value read instead.
		α ReadJoined( sp<UAClient> client, flat_set<NodeId> nodes )ι->VoidTask;
		//What the group takes from each new item's node:  its DataType, which the group's edits take (Group::SetDataType), as the
		//pass-through types its edits, ValueTypesAwait, but none for an abstract one of namespace 0, as Number or BaseDataType,
		//whose values may be any subtype's;  whether that is an enumeration, by its built-in kind for one of namespace 0 open62541
		//has, else by the server's definition of it (EnumTypeCache);  and its InstrumentRange and EURange, which a percent-of-range
		//band takes (ForGroup).  What it can't read keeps what the node had:  where an answer was lost, not refused, the watch asks
		//again (Redescribe), its band waiting meanwhile.  `again`:  the watch's, whose losses were warned of once already.
		α Describe( sp<UAClient> client, flat_set<NodeId> nodes, bool again=false )ι->VoidTask;
		//The rebuild's check (ConnectionRestored):  the server's namespaces read again, and each member's URI looked up in them.
		//Each at the index its item was made by:  the group is told the connection is back and takes what waited.  One moved, or
		//the array unread:  every item goes, and the members are asked for again by URI.
		α Verify( uint epoch )ι->VoidTask;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Moved( const Namespaces& namespaces )Ι->bool;//a member's URI at another index than its item's.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Reset()ι->void;//every member unsubscribed and its index forgotten, for a pass to resolve again.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Watch()ι->void;//arms the watch, every RetryInterval until Close.
		α Check()ι->void;//the watch's:  members whose items are gone without a word, those lost past AbandonAfter, and Redescribe.

		const HistGroupPK _id;
		const string _guid;
		const ServerCnnctnNK _slug;
		const sp<Opc::Hist::Group> _group;
		const Credential _collector;
		Opc::Hist::IClock& _clock;
		mutable absl::Mutex _mutex;//before the group's own:  Enqueue runs under it, so values reach the group in the order they came.
		string _name ABSL_GUARDED_BY(_mutex);
		HistThresholdColumns _columns ABSL_GUARDED_BY(_mutex);
		flat_map<HistGroupNodePK,Node> _nodes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<NodeId,HistGroupNodePK> _byNode ABSL_GUARDED_BY(_mutex);//by the server's index, once a connection gave it.
		Namespaces _namespaces ABSL_GUARDED_BY(_mutex);//the NamespaceArray as last read, where each member's URI is at its item's index.
		uint _epoch ABSL_GUARDED_BY(_mutex){};//one per break and return, so a check that a later one overtook does nothing.
		bool _verifying ABSL_GUARDED_BY(_mutex){};//a rebuild's check is out:  values wait in _held.
		vector<std::pair<HistGroupNodePK,Value>> _held ABSL_GUARDED_BY(_mutex);
		Opc::Hist::IClock::TimerId _retry ABSL_GUARDED_BY(_mutex){};
		Opc::Hist::IClock::TimerId _watch ABSL_GUARDED_BY(_mutex){};
		bool _subscribing ABSL_GUARDED_BY(_mutex){}, _again ABSL_GUARDED_BY(_mutex){}, _closed ABSL_GUARDED_BY(_mutex){};//_again:  a member joined while a pass was out, which runs another.
	};
}
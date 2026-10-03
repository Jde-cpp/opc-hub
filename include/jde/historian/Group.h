#pragma once
#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "Clock.h"

namespace Jde::Opc::Hist{
	using NodeIndex = uint32;
	struct Range{ double Low; double High; };

	//A node's HistoricalDataConfiguration, resolved by the host: OpcServer's from the node's HA Configuration with the
	//defaults filled in, the gateway's from the hist_group_nodes row, then its template member, then its group.  The
	//library inherits nothing, so a template or group edit reaches it as a SetThresholds per affected node.
	struct Thresholds{
		optional<double> ExceptionDeviation;//none stores every change.
		UA_ExceptionDeviationFormat DeviationFormat{ UA_EXCEPTIONDEVIATIONFORMAT_ABSOLUTEVALUE };
		Duration MinTimeInterval{};
		Duration MaxTimeInterval{};//the heartbeat; 0 is off.
		bool Stepped{ true };
		optional<Hist::Range> Range;//InstrumentRange or EURange, whichever DeviationFormat needs; without it, every change is stored.
	};

	//Who made a membership change or an edit, resolved by the host: OpcServer through OpcAuthorize, the gateway through
	//Authorize and the caller's QL creds.  The name is the one the identity has now, so a later rename never rewrites it.
	struct Writer{
		//A file's identity_id is 32 bits, as access_identities' is.  UserPK::System, wider on Linux, is stored as UINT32_MAX,
		//its value on Windows, so any other id from UINT32_MAX up throws.
		Writer( UserPK identityId, string userName, SRCE )ε;
		UserPK IdentityId;
		string UserName;
	};

	struct Member{
		ExNodeId Node;//with its namespace URI, which is what the group matches on.
		Thresholds Config;
		NodeIndex Index{};//the gateway's hist_group_nodes row id, 32 bits as a file's node_index is; 0 in a group that issues its own.
	};

	enum class EIndexes : uint8{
		Issued,//OpcServer: the historian issues node_index itself, never twice.
		Host	//the gateway: node_index is the node's hist_group_nodes row id.
	};
	struct GroupConfig{
		string Name;//the stem of the group's files: "server" on OpcServer, the group's guid on the gateway.
		EIndexes Indexes{ EIndexes::Host };
		//How long a flush holds back a fresh heartbeat: the collector's publishing interval, or 0 where values arrive as
		//they are written (OpcServer).
		Duration PublishingInterval{};
	};
	//What a group's files say at start, which #203 reads from its newest file:  each member's index, and the index an
	//Issued group issues next, from FileStart.
	struct Restored{
		flat_map<ExNodeId,NodeIndex> Members;
		NodeIndex NextIndex{ 1 };
	};

	//What a group holds until a flush writes it.  Each record's time is a source time:  Ts for a membership change.
	struct NodeAdded{ NodeIndex Index; ExNodeId Node; TimePoint Ts; optional<Writer> By; };
	struct NodeRemoved{ NodeIndex Index; TimePoint Ts; optional<Writer> By; };
	//Break is set on a node's first value after a break, which the flush judges against it.  Unsupported is set on a
	//node's first value that a file can't hold (ProtoUtils::Supported), which the flush warns of:  Enqueue never logs.
	struct DataValue{ NodeIndex Index; Value Data; optional<TimePoint> Break; bool Unsupported{}; };
	using Record = variant<NodeAdded,NodeRemoved,DataValue>;

	//One node group, written to its own files.  Enqueue is the collection path:  OpcServer calls it under open62541's
	//service lock, and the gateway on the connection's strand under the monitoring lock.  So it takes only the group's
	//buffer lock - it never waits on I/O, a flush or a /hist snapshot - and never calls back into the host.  The rest is
	//the host telling the group about membership, thresholds and its connection.
	struct Group final : noncopyable{
		//members is the host's whole membership at start, checked against restored:  a member the files hold keeps its
		//index, a new one is added, and one the files hold that isn't in members is removed.  None carries a writer.
		Group( GroupConfig config, sp<IClock> clock, vector<Member> members={}, Restored restored={}, SRCE )ε;
		α Name()Ι->const string&{ return _config.Name; }

		//Returns the node's index:  issued here when the group issues its own, otherwise member.Index.  A node that left
		//rejoins with a break at its removal time, or at the break it left with.
		α Add( Member member, optional<Writer> by={}, SRCE )ε->NodeIndex;
		α Remove( NodeIndex index, optional<Writer> by={}, SRCE )ε->void;
		α SetThresholds( NodeIndex index, Thresholds thresholds, SRCE )ε->void;
		α FindThresholds( NodeIndex index )Ι->optional<Thresholds>;
		α Find( const ExNodeId& node )Ι->optional<NodeIndex>;

		//false for a node that isn't in the group, such as one a racing Remove took out.  A value with no server
		//timestamp is stamped with the time it arrived.
		α Enqueue( NodeIndex index, const UA_DataValue& value )ι->bool;

		//The gateway's connection-state callback, with the time the connection broke.  Each member keeps the first break
		//until its next value, which carries it into the buffer.  OpcServer never calls these: its only breaks are stops
		//and crashes, which its next start finds.
		α Disconnected( TimePoint at )ι->void;
		α Connected()ι->void;
		α IsConnected()Ι->bool;
		α FindBreak( NodeIndex index )Ι->optional<TimePoint>;//the one the node's next value will carry.

		α Buffer()Ι->vector<Record>;//a copy, as a /hist snapshot takes it.
		//Historian::RemoveGroup's:  every member leaves, and Add throws after.  The buffer stays for the flush.
		α Close( optional<Writer> by )ι->void;
	private:
		struct Node{ ExNodeId Id; Thresholds Config; optional<TimePoint> Break; bool Unsupported{}; };
		α Issued()Ι->bool{ return _config.Indexes==EIndexes::Issued; }
		α Normalize( Member& member, SL sl )Ε->ExNodeId;
		//index 0 issues the next one.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Insert( ExNodeId node, Thresholds config, NodeIndex index, SL sl )ε->NodeIndex;
		sp<IClock> _clock;
		const GroupConfig _config;
		mutable absl::Mutex _mutex;
		//In index order, so whatever walks it to write records writes them the same way every run, and a B-tree, so loading
		//and Enqueue's lookup stay logarithmic.  The maps by NodeId are hashed:  nothing walks them.
		absl::btree_map<NodeIndex,Node> _nodes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,NodeIndex> _indexes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,TimePoint> _left ABSL_GUARDED_BY(_mutex);//each removed node's break, which a rejoin takes.
		NodeIndex _nextIndex ABSL_GUARDED_BY(_mutex){ 1 };
		bool _connected ABSL_GUARDED_BY(_mutex){ true };
		bool _closed ABSL_GUARDED_BY(_mutex){};
		vector<Record> _buffer ABSL_GUARDED_BY(_mutex);
	};
}
#pragma once
#include <deque>
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
	//What a group holds until a flush writes it.  Each record's time is a source time:  Ts for a membership change.
	struct NodeAdded{ NodeIndex Index; ExNodeId Node; TimePoint Ts; optional<Writer> By; };
	struct NodeRemoved{ NodeIndex Index; TimePoint Ts; optional<Writer> By; };
	//Break is set on a node's first value after a break, which the flush judges against it.  Unsupported is set on a
	//node's first value that a file can't hold (ProtoUtils::Supported), which the flush warns of:  Enqueue never logs.
	struct DataValue{ NodeIndex Index; Value Data; optional<TimePoint> Break; bool Unsupported{}; };
	using Record = variant<NodeAdded,NodeRemoved,DataValue>;

	struct GroupFiles;
	struct Run;
	struct Store;

	//One node group, written to its own files.  Enqueue is the collection path:  OpcServer calls it under open62541's
	//service lock, and the gateway on the connection's strand under the monitoring lock.  So it takes only the group's
	//buffer lock - it never waits on I/O, a flush or a /hist snapshot - and never calls back into the host.  The rest is
	//the host telling the group about membership, thresholds and its connection.
	struct Group final : noncopyable, std::enable_shared_from_this<Group>{
		//Historian::AddGroup's.  members is the host's whole membership at start, checked against what the group's newest
		//file holds:  a member the file holds keeps its index, with the group's last flush as its break, a new one is added,
		//and one the file holds that isn't in members is removed.  None carries a writer.  Throws when that file can't be
		//read.
		Group( GroupConfig config, sp<Store> store, vector<Member> members={}, SRCE )ε;
		~Group();
		α Name()Ι->const string&{ return _config.Name; }

		//Returns the node's index:  issued here when the group issues its own, otherwise member.Index.  A node that left
		//rejoins with a break at its removal time, or at the break it left with.
		α Add( Member member, optional<Writer> by={}, SRCE )ε->NodeIndex;
		α Remove( NodeIndex index, optional<Writer> by={}, SRCE )ε->void;
		α SetThresholds( NodeIndex index, Thresholds thresholds, SRCE )ε->void;
		α FindThresholds( NodeIndex index )Ι->optional<Thresholds>;
		α Find( const ExNodeId& node )Ι->optional<NodeIndex>;

		//false for a node that isn't in the group, such as one a racing Remove took out.  A value with no server
		//timestamp is stamped with the time it arrived.  A buffer that reaches 8 KB asks the clock for a flush.
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

		//Writes what the group buffered:  sorted by source time, each record to its own day's file, each file fsynced, and
		//then the group's .flushed.  The clock runs it when the buffer reaches 8 KB and every `delay`, so a host needn't.
		//False when it didn't write all it took.  The records of a file that couldn't be written, and those of later days,
		//go back to the buffer, for `delay` to try again; those of a file the historian won't append to are dropped.
		α Flush( SRCE )ι->bool;
		//The last flush that wrote all it took, which is the group's break if the process stops here.
		α Flushed()Ι->optional<TimePoint>;
		//A day's live file's, once the process has opened it:  as its first-open scan would rebuild them.
		α Runs( std::chrono::year_month_day day )Ι->vector<Run>;
	private:
		friend struct Historian;
		friend struct Store;
		struct Node{ ExNodeId Id; Thresholds Config; optional<TimePoint> Break; bool Unsupported{}; };
		//Bytes is what the record takes in a file, near enough.  Copied is a membership change whose copies for later
		//days' files are made, or one of those copies.
		struct Buffered{ Record Item; uint Sequence; uint32_t Bytes; bool Copied{}; };
		//What a full buffer dropped of a node, until a flush marks it:  a Bad_DataLost at the earliest source time among
		//them, then the one with the latest, written back.
		struct Lost{ Value Marker{ (StatusCode)UA_STATUSCODE_BADDATALOST }; optional<Buffered> Newest; uint Count{}; };

		α Issued()Ι->bool{ return _config.Indexes==EIndexes::Issued; }
		α Normalize( Member& member, SL sl )Ε->ExNodeId;
		//index 0 issues the next one.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Insert( ExNodeId node, Thresholds config, NodeIndex index, SL sl )ε->NodeIndex;
		//True once every group's buffer together is past maxBuffer.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Push( Record&& record, uint32_t bytes )ι->bool;
		//True when the buffer has reached 8 KB and no flush for that is on its way.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Full()ι->bool;
		//What follows a Push, once _mutex is released.
		α Pushed( bool flush, bool over )ι->void;
		α Schedule( Duration after )ι->IClock::TimerId;
		//The buffer, for a flush:  each node's lost values first, as a marker and the one written back, then the rest as
		//they arrived.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Take()ι->vector<Buffered>;
		//What a flush couldn't write, back to the front of the buffer.  True as Push.
		α Return( vector<Buffered>&& records )ι->bool;
		α Start()ι->void;//arms `delay`, once the group is shared.
		α Stop()ι->void;//the historian's end:  takes no more, writes what it holds, and flushes no more.
		α Idle()Ι->bool;//removed, and all it buffered written.
		//The Store's, to trim the buffers:  the group's oldest value, and dropping those older than before until need
		//bytes are freed.  Returns how many it dropped.
		α Oldest()Ι->optional<uint>;
		α Drop( uint before, uint need )ι->uint;

		const sp<Store> _store;
		const GroupConfig _config;
		//The group's write lock:  a flush holds it from taking the buffer through writing .flushed.  Taken before _mutex,
		//never under it.
		mutable absl::Mutex _writeMutex;
		up<GroupFiles> _files ABSL_PT_GUARDED_BY(_writeMutex);
		IClock::TimerId _timer ABSL_GUARDED_BY(_writeMutex){};//`delay`'s.
		bool _ended ABSL_GUARDED_BY(_writeMutex){};
		mutable absl::Mutex _mutex;
		//In index order, so whatever walks it to write records writes them the same way every run, and a B-tree, so loading
		//and Enqueue's lookup stay logarithmic.  The maps by NodeId are hashed:  nothing walks them.
		absl::btree_map<NodeIndex,Node> _nodes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,NodeIndex> _indexes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,TimePoint> _left ABSL_GUARDED_BY(_mutex);//each removed node's break, which a rejoin takes.
		//Each index that left, which a file holding a value of it may yet need to map.
		absl::flat_hash_map<NodeIndex,ExNodeId> _gone ABSL_GUARDED_BY(_mutex);
		NodeIndex _nextIndex ABSL_GUARDED_BY(_mutex){ 1 };
		bool _connected ABSL_GUARDED_BY(_mutex){ true };
		bool _closed ABSL_GUARDED_BY(_mutex){};
		bool _stopped ABSL_GUARDED_BY(_mutex){};
		//The buffer, each part in the order it arrived:  membership changes, which are never dropped, and values.
		vector<Buffered> _changes ABSL_GUARDED_BY(_mutex);
		std::deque<Buffered> _values ABSL_GUARDED_BY(_mutex);
		uint _bytes ABSL_GUARDED_BY(_mutex){};//the buffer's, which the flush at 8 KB counts.
		absl::btree_map<NodeIndex,Lost> _lost ABSL_GUARDED_BY(_mutex);
		bool _requested ABSL_GUARDED_BY(_mutex){};//a flush for the buffer's size is on its way.
		bool _failing ABSL_GUARDED_BY(_mutex){};//the last flush couldn't write, so only `delay` tries again.
	};
}
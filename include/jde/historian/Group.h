#pragma once
#include <deque>
#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <jde/fwk/co/AnyAwait.h>
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
		//its value on Windows, so any other id from UINT32_MAX up throws, as does a name that isn't UTF-8.
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
	//node's first value that a file can't hold (ProtoUtils::Supported), and on its first with text that isn't UTF-8
	//(ProtoUtils::Utf8), which the flush warns of:  Enqueue never logs.
	struct DataValue{ NodeIndex Index; Value Data; optional<TimePoint> Break; bool Unsupported{}; };
	using Record = variant<NodeAdded,NodeRemoved,DataValue>;

	struct Group;
	struct GroupFiles;
	struct Run;
	struct Store;

	//What Group::Flush and Settled return, which any coroutine can co_await, and BlockAny waits on from a thread that
	//isn't the executor's.  It resumes on the thread that finished the flush.
	struct FlushAwait final : AnyAwait<bool>{
		FlushAwait( sp<Group> group, bool flush, SL sl )ι:AnyAwait<bool>{ sl }, _group{ move(group) }, _flush{ flush }{}
		friend struct Group;//Request reads _flush.
		//Completes here, without suspending, when there is nothing to wait for:  so a coroutine that awaits it in a loop
		//doesn't nest a resume, and its stack, per turn.
		α await_ready()ι->bool override;
	protected:
		α Suspend()ι->void override;
	private:
		sp<Group> _group;
		bool _flush;
	};

	//One node group, written to its own files.  Enqueue is the collection path:  OpcServer calls it under open62541's
	//service lock, and the gateway on the connection's strand under the monitoring lock.  So it takes only the group's
	//buffer lock - it never waits on I/O, a flush or a /hist snapshot - and never calls back into the host.  The rest is
	//the host telling the group about membership, thresholds and its connection.  A flush writes through IO::WriteAwait,
	//so it holds no thread while a write or its fsync is out, and no lock.
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
		//timestamp is stamped with the time it arrived.  A timestamp no day holds, before 1601 or past 9999 as DateTime's
		//MaxValue is, counts as none.  A buffer that reaches 8 KB asks the clock for a flush, and one that
		//takes the buffers past maxBuffer for a flush or a trim.
		α Enqueue( NodeIndex index, const UA_DataValue& value )ι->bool;

		//The gateway's connection-state callback, with the time the connection broke.  Each member keeps the first break
		//until its next value, which carries it into the buffer.  OpcServer never calls these: its only breaks are stops
		//and crashes, which its next start finds.
		α Disconnected( TimePoint at )ι->void;
		α Connected()ι->void;
		α IsConnected()Ι->bool;
		α FindBreak( NodeIndex index )Ι->optional<TimePoint>;//the one the node's next value will carry.

		//A copy, as a /hist snapshot takes it.  What a running flush took is no longer here, and not yet in its files.
		α Buffer()Ι->vector<Record>;
		//Historian::RemoveGroup's:  every member leaves, and Add throws after.  The buffer stays for the flush, which also
		//rewrites each live file the group has, today's included, as its archive:  no midnight comes for a group that is
		//gone, and no start adds it again.
		α Close( optional<Writer> by )ι->void;

		//Writes what the group buffered:  sorted by source time, each record to its own day's file, each file fsynced, and
		//then the group's .flushed.  The clock runs it when the buffer reaches 8 KB and every `delay`, so a host needn't.
		//One flush runs at a time:  this one follows any that is running, and takes what is buffered when it starts.
		//False when it didn't write all it took.  The records of a day whose file couldn't be written go back to the buffer
		//while the other days are written, and the clock tries that day again once `delay` is up, this at once.  Those of a
		//file the historian won't write to are dropped.
		//
		//A flush is also the turn in which a day's file becomes its archive, rewritten in source-time order:  `delay` after
		//midnight in timeZone, when the clock runs one for it, and at the start for a midnight the process was down for.
		//A record for a day already archived is merged into place by a rewrite too.  The clock's flushes do that at most
		//once per `delay` for a day, holding its records meanwhile, and this one at once.
		α Flush( SRCE )ι->FlushAwait;
		//Asks for no flush:  resumes once none is running or waiting to, at once when none is.  False when the last one
		//it waited on didn't write all it took.
		α Settled( SRCE )ι->FlushAwait;
		//The last flush that wrote all it took, which is the group's break if the process stops here.
		α Flushed()Ι->optional<TimePoint>;
		//A day's live file's, once the process has opened it:  as its first-open scan would rebuild them.
		α Runs( std::chrono::year_month_day day )Ι->vector<Run>;
	private:
		friend struct FlushAwait;
		friend struct Historian;
		friend struct Store;
		struct Node{ ExNodeId Id; Thresholds Config; optional<TimePoint> Break; bool Unsupported{}; bool NotUtf8{}; };
		//Bytes is what the record takes in a file, near enough.  Copied is a membership change whose copies for later
		//days' files are made, or one of those copies.
		struct Buffered{ Record Item; uint Sequence; uint32_t Bytes; bool Copied{}; };
		Ω Cost( const Buffered& b )ι->uint{ return b.Bytes+sizeof(Buffered); }//what a buffered record counts against maxBuffer.
		//What a full buffer dropped of a node, until a flush marks it:  a Bad_DataLost at the earliest source time among
		//them, then the one with the latest, written back.
		struct Lost{ Value Marker{ (StatusCode)UA_STATUSCODE_BADDATALOST }; optional<Buffered> Newest; uint Count{}; };

		α Issued()Ι->bool{ return _config.Indexes==EIndexes::Issued; }
		α Normalize( Member& member, SL sl )Ε->ExNodeId;
		//index 0 issues the next one.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Insert( ExNodeId node, Thresholds config, NodeIndex index, SL sl )ε->NodeIndex;
		//Buffers record:  true once every group's buffer together is past maxBuffer.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Hold( Record&& record, uint32_t bytes )ι->bool;
		//True when the buffer has reached 8 KB, or over, every group's together past maxBuffer, and no flush for that is on
		//its way:  a group that can write is flushed, not trimmed.  It then claims the flush, so the next push doesn't ask
		//for another before it takes the buffer.  A group whose last flush only held records for an archive's next
		//rewrite can write, so over still claims one, which merges them.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α ClaimFlush( bool over )ι->bool;
		//What a push asks for, once _mutex is released:  a flush, or a trim when the buffers are past maxBuffer and no
		//flush claimed will take this group's.
		struct Pushing{ bool Flush; bool Trim; };
		//Add's, Remove's and Enqueue's:  Hold, then ClaimFlush.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Push( Record&& record, uint32_t bytes )ι->Pushing;
		α Pushed( Pushing pushing )ι->void;
		α Schedule( Duration after )ι->IClock::TimerId;
		//The clock's flush `delay` after the next midnight in timeZone, by when the day's last flush has landed:  it
		//rewrites the day's file as its archive.
		α ScheduleMidnight()ι->IClock::TimerId;
		α Midnight()ι->void;//that timer's:  the next midnight's, and the flush.
		//The group's timers, which Disarm takes off it under _mutex, for Cancel to cancel outside it.
		struct Timers final{ IClock::TimerId Delay{}; IClock::TimerId Midnight{}; };
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Disarm()ι->Timers{ return { std::exchange(_timer, 0), std::exchange(_midnight, 0) }; }
		α Cancel( Timers timers )ι->void;
		//After a flush that wrote all it took:  forgets each node that left whose values are all written, since no other
		//can arrive for it.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α PruneGone()ι->void;
		//A value flagged for the flush to warn of, dropped without being written back:  its node's next of its kind is
		//flagged instead, so the warning isn't lost with it.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Unflag( const DataValue& gone )ι->void;
		//Each node's lost values into y, as a marker and the one written back, emptying _lost.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α MarkLost( vector<Buffered>& y )ι->void;
		//The buffer, for a flush:  each node's lost values first, as a marker and the one written back, then the rest as
		//they arrived.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Take()ι->vector<Buffered>;
		//What a flush couldn't write, back to the front of the buffer, without counting toward the flush at 8 KB, followed by
		//the marker and value of each gap dropped while it was out.  True as Push.
		α Return( vector<Buffered>&& records )ι->bool;
		//A Flush or a Settled awaiter's:  waits for the next flush to start and end, or for none to be running.
		α Request( FlushAwait& waiter )ι->void;
		//The clock's flush, which no one waits on, unless one is running:  that one then runs another after.  Each I/O step
		//of a flush logs against its own line, which a clock-driven one has no caller's to give.
		α Request()ι->void;
		α Waits( bool flush )Ι->bool;//what Request would wait for:  a flush, or one running, and the group not ended.
		//Flushes until no other was asked for meanwhile.  self keeps the group for as long as its writes are out.
		α Flushing( sp<Group> self )ι->VoidTask;
		//Arms `delay` and midnight, once the group is shared, and asks for a flush when its start found files that a
		//midnight left live.
		α Start()ι->void;
		//The historian's end, in two steps so every group's last flush runs at once.  Stopping takes no more, disarms
		//`delay` and starts the last flush on the executor.  Stopped waits for it, and any already running, until
		//deadline, then flushes no more:  a flush still out starts no write after, a rename included, though one the OS
		//already has finishes.  Neither waits on an executor that isn't running, where no write returns.
		α Stopping()ι->void;
		α Stopped( std::chrono::steady_clock::time_point deadline )ι->void;
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α Written()Ι->bool;//removed, all it buffered taken by a flush, and its files archives.
		//Once a removed group is written, with no flush running, nothing of it is left to reach its files:  it then flushes
		//no more, so neither the flush its Close asked for nor a host that kept it writes after its name is let go, and
		//returns true.
		α EndIfWritten()ι->bool;
		//Whether the group has ended:  a flush the historian's end gave up on checks it before each write it starts, so none
		//starts once the lock is let go.
		α Ended()Ι->bool;
		//The Store's, to trim the buffers:  the group's oldest value, and dropping those older than before until need
		//bytes are freed.  Returns how many it dropped, and sets began when they begin the group's streak of drops, which
		//the Store warns of once.
		α Oldest()Ι->optional<uint>;
		α Drop( uint before, uint need, bool& began )ι->uint;

		const sp<Store> _store;
		const GroupConfig _config;
		//What the group knows of its files.  A flush takes it for each step between its writes, never across one, and
		//before _mutex, never under it.
		mutable absl::Mutex _filesMutex;
		up<GroupFiles> _files ABSL_PT_GUARDED_BY(_filesMutex);
		mutable absl::Mutex _mutex;
		IClock::TimerId _timer ABSL_GUARDED_BY(_mutex){};//`delay`'s.
		IClock::TimerId _midnight ABSL_GUARDED_BY(_mutex){};
		bool _ended ABSL_GUARDED_BY(_mutex){};//no flush starts.
		bool _flushing ABSL_GUARDED_BY(_mutex){};//a flush is running, the only one that changes _files.
		bool _again ABSL_GUARDED_BY(_mutex){};//the clock asked for another meanwhile.
		vector<FlushAwait*> _waiters ABSL_GUARDED_BY(_mutex);//each waits on the next flush to start.
		vector<FlushAwait*> _settling ABSL_GUARDED_BY(_mutex);//each waits for no flush to be running.
		//In index order, so whatever walks it to write records writes them the same way every run, and a B-tree, so loading
		//and Enqueue's lookup stay logarithmic.  The maps by NodeId are hashed:  nothing walks them.
		absl::btree_map<NodeIndex,Node> _nodes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,NodeIndex> _indexes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,TimePoint> _left ABSL_GUARDED_BY(_mutex);//each removed node's break, which a rejoin takes.
		//Each index that left, which a file holding a value of it may yet need to map, until those values are written.
		absl::flat_hash_map<NodeIndex,ExNodeId> _gone ABSL_GUARDED_BY(_mutex);
		NodeIndex _nextIndex ABSL_GUARDED_BY(_mutex){ 1 };
		bool _connected ABSL_GUARDED_BY(_mutex){ true };
		bool _closed ABSL_GUARDED_BY(_mutex){};
		bool _archived ABSL_GUARDED_BY(_mutex){};//as the last flush left the group's files:  none live.
		bool _stopped ABSL_GUARDED_BY(_mutex){};
		//The buffer, each part in the order it arrived:  membership changes, which are never dropped, and values.
		vector<Buffered> _changes ABSL_GUARDED_BY(_mutex);
		std::deque<Buffered> _values ABSL_GUARDED_BY(_mutex);
		uint _held ABSL_GUARDED_BY(_mutex){};//what _changes and _values count against maxBuffer, each record's Cost.
		uint _fresh ABSL_GUARDED_BY(_mutex){};//what arrived since a flush last took the buffer, which the flush at 8 KB counts.
		absl::btree_map<NodeIndex,Lost> _lost ABSL_GUARDED_BY(_mutex);
		bool _requested ABSL_GUARDED_BY(_mutex){};//a flush for the buffer's size is on its way.
		bool _dropping ABSL_GUARDED_BY(_mutex){};//values were dropped since the last flush that wrote all it took.
		bool _failing ABSL_GUARDED_BY(_mutex){};//the last flush held records back and wrote none, so only `delay` tries again.
		//The last flush wrote none, and held records only for an archive rewritten less than `delay` before:  `delay` tries
		//again, or the buffers passing maxBuffer, or a record for another day, which the flush at 8 KB can write.
		bool _deferred ABSL_GUARDED_BY(_mutex){};
		flat_set<std::chrono::year_month_day> _deferredDays ABSL_GUARDED_BY(_mutex);//the archives it held records for.
		//Each day a flush couldn't write, and when the clock's flushes may try it again:  a day that stays unwritable is
		//tried once a `delay`, not at every 8 KB the others reach.
		flat_map<std::chrono::year_month_day,TimePoint> _failingDays ABSL_GUARDED_BY(_filesMutex);
	};
}
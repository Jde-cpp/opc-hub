#pragma once
#include <deque>
#include <queue>
#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <absl/functional/function_ref.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "Clock.h"
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Records.pb.h>
ENABLE_WARNINGS

namespace Jde::Opc::Hist{
	using NodeIndex = uint32;
	struct Range{ double Low; double High; };

	//A node's HistoricalDataConfiguration, resolved by the host: OpcServer's from the node's HA Configuration with the
	//defaults filled in, the gateway's from the hist_group_nodes row, then its template member, then its group.  The
	//library inherits nothing, so a template or group edit reaches it as a SetThresholds per affected node.
	struct Thresholds{
		//None stores every change.  The band is for numeric scalars, which an enumeration's values arrive as, Int32:  the
		//host leaves it unset for a node whose DataType is one.
		optional<double> ExceptionDeviation;
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
	//A value that passed the compression test, a Bad_DataLost marker, or a heartbeat, whose Heartbeat is the
	//SourceTimestamp of the value it repeats, or with Unsourced that value's server timestamp:  it came with no
	//SourceTimestamp.  Unsupported is set on a node's first value that a file can't hold (ProtoUtils::Supported), and on
	//its first with text that isn't UTF-8 (ProtoUtils::Utf8), which the flush warns of:  Enqueue never logs.
	struct DataValue{ NodeIndex Index; Value Data; optional<UA_DateTime> Heartbeat; bool Unsupported{}; bool Unsourced{}; };
	using Record = variant<NodeAdded,NodeRemoved,DataValue>;

	//A raw read, Part 11's ReadRawModifiedDetails over the group's files and buffer (spec *Reads*).  Times are UA ticks,
	//as the records' are.  At least one of Start and End:  Start alone reads forward from it, End alone backward from
	//it, and a Start after End reads the range in reverse, later values first.  With both, each is inside the range.
	struct ReadRequest{
		vector<NodeIndex> Nodes;//in the order Continuation counts them.
		optional<UA_DateTime> Start;
		optional<UA_DateTime> End;
		//Each node's value at or before the earlier end of the range and its first record at or after the later:  a record
		//at the time itself serves as the bound, else the one before or after, else Bad_BoundNotFound at that time.  An
		//open end has none.
		bool Bounds{};
		//The modified values with their ModificationInfo instead, as Part 11 §6.5.3 defines them:  for an INSERT the value
		//inserted, for anything else the value it replaced, by the time they target.  No bounds:  a read that asks for both
		//throws a UAException with Bad_InvalidArgument.
		bool Modified{};
		uint Limit{};//the most values a page holds; 0, or more than hist.readLimit, is readLimit.
		string Continuation;//the page before's, empty for the first.
		//Ends the page with the first day whose file it reads through while another is left to read, however few values it
		//holds, none included:  OpcServer's, whose read runs inside open62541's service lock.  A reverse read then goes
		//back a day a page, or jumps as it would.  The bounds still look where they must.
		bool OneDay{};
	};
	//Part 11's ModificationInfo, as the Modification record stored it:  who made the edit, when, and what it did.
	struct ModificationInfo{ UA_DateTime Time; Proto::UpdateType Type; string UserName; optional<uint32_t> IdentityId; };
	struct ReadValue{
		Proto::DataValue Value;//node_index set, and a heartbeat marked as the record marks it.
		bool Bound{};//one of the Bounds asked for, which counts toward Limit.
		optional<ModificationInfo> Modification;//a modified read's.
	};
	struct ReadResult{
		vector<ReadValue> Values;//in source-time order, later first in a reverse read.
		string Continuation;//for the next page, empty on the last.
		bool NoData{};//the last page of a read none of whose pages returned a value:  UA's Good_NoData.
	};
	//A read's value as UA has it, for a host that serves HistoryRead.  What a record doesn't store, a Good status, zero
	//picoseconds or a null value, comes back with its mask's bit clear.  Throws for a value this build can't decode.
	α ToUA( const Proto::DataValue& v )ε->Value;

	//Part 11's HistoryUpdate (spec *Edits*), one entry per node, as the service takes them.  Times are UA ticks.
	//UpdateDataDetails:  each value is keyed by its SourceTimestamp, which it must carry.  INSERT refuses a time that holds
	//a record of the node, REPLACE one that holds none, and UPDATE does either.  REMOVE belongs to annotations and is
	//refused.
	struct UpdateData{ NodeIndex Node; UA_PerformUpdateType Type; vector<Value> Values; };
	//DeleteRawModifiedDetails with isDeleteModified false:  every record of the node from Start through End, both held, as
	//a read's range holds them.  A Start after End is refused.
	struct DeleteRaw{ NodeIndex Node; UA_DateTime Start; UA_DateTime End; };
	//DeleteAtTimeDetails:  every record of the node at each time.
	struct DeleteAtTime{ NodeIndex Node; vector<UA_DateTime> Times; };
	using EditDetails = variant<UpdateData,DeleteRaw,DeleteAtTime>;
	//Part 11's HistoryUpdateResult:  the entry's status, and one per value or time, in the request's order, for an
	//UpdateData or a DeleteAtTime.  Good_EntryInserted and Good_EntryReplaced say what an UpdateData did, Bad_EntryExists
	//and Bad_NoEntryExists what it refused; a DeleteRaw that found nothing in its range is Bad_NoData, and a DeleteAtTime
	//time that holds nothing Bad_NoEntryExists.
	struct EditResult{ StatusCode Status{ UA_STATUSCODE_GOOD }; vector<StatusCode> Results; };

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

	//What Group::Edit returns:  resumes, on the thread that wrote the edit, once its records are durable.
	struct EditAwait final : AnyAwait<vector<EditResult>>{
		EditAwait( sp<Group> group, vector<EditDetails> details, Writer by, SL sl )ι:AnyAwait<vector<EditResult>>{ sl }, _group{ move(group) }, _details{ move(details) }, _by{ move(by) }{}
		friend struct Group;
	protected:
		α Suspend()ι->void override;
	private:
		sp<Group> _group;
		vector<EditDetails> _details;
		Writer _by;
	};

	//One node group, written to its own files.  Enqueue is the collection path:  OpcServer calls it under open62541's
	//service lock, and the gateway on the connection's strand under the monitoring lock.  So it takes only the group's
	//buffer lock - it never waits on I/O, a flush or a /hist snapshot - and never calls back into the host.  The rest is
	//the host telling the group about membership, thresholds and its connection.  A flush writes through IO::WriteAwait,
	//so it holds no thread while a write or its fsync is out, and no lock.
	struct Group final : noncopyable, std::enable_shared_from_this<Group>{
		//Historian::AddGroup's.  members is the host's whole membership at start, checked against what the group's newest
		//file holds:  a member the file holds keeps its index, with the group's last flush as its break when the files hold a
		//record of it, a new one is added, and one the file holds that isn't in members is removed.  None carries a writer.  Throws when that file can't be
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
		//
		//Only what the node's thresholds store is buffered.  A change inside the deviation band of the last stored value,
		//with its status code, is dropped.  One that passes within MinTimeInterval of the last stored value, by their
		//source times, is held as the node's pending value, which each later change replaces, and stored when the interval
		//ends if it still passes.  The clock re-stores the last value of a node that stored nothing for its
		//MaxTimeInterval, as a heartbeat, while the connection is up.
		//
		//A node's first value after a break is judged by Part 11 §4.3's comparison instead, against the SourceTimestamp of
		//the last value it delivered, or after a start of its newest record in the group's files:  equal stores nothing,
		//later stores a Bad_DataLost marker no later than the value and then the value, and earlier the marker alone, at
		//the break.  Equal is later when the value doesn't agree with the last stored one, the same but for what the
		//thresholds drop:  a source can change a value inside one timestamp.  The marker is never before the node's newest record, which is in the source's clock as the break
		//isn't, unless the value itself is.
		α Enqueue( NodeIndex index, const UA_DataValue& value )ι->bool;

		//The gateway's connection-state callbacks:  the first with the time the connection broke, the second once it is
		//back, before any value the new subscription delivers.  Each member keeps the first break until its first value
		//after Connected, which is judged against it, and a pending value is settled as at its interval's end.  A value
		//that arrives between the two was in flight when the connection broke:  it takes the ordinary test, and the break
		//stands.  A callback that comes late drops each heartbeat made at or after that time, unless a flush already
		//took it.  OpcServer never calls these: its only breaks are stops and crashes, which its next start finds.
		α Disconnected( TimePoint at )ι->void;
		α Connected()ι->void;
		α IsConnected()Ι->bool;
		α FindBreak( NodeIndex index )Ι->optional<TimePoint>;//the one the node's first value after it is judged against.

		//A copy, as a /hist snapshot takes it:  what will be stored, a pending value not yet among it.  What a running
		//flush took is no longer here, and not yet in its files.
		α Buffer()Ι->vector<Record>;
		//Historian::RemoveGroup's:  every member leaves, and Add throws after.  The buffer stays for the flush, which also
		//rewrites each live file the group has, today's included, as its archive:  no midnight comes for a group that is
		//gone, and no start adds it again.
		α Close( optional<Writer> by )ι->void;

		//Writes what the group buffered:  sorted by source time, each record to its own day's file, each file fsynced, and
		//then the group's .flushed.  The clock runs it when the buffer reaches 8 KB and every `delay`, so a host needn't.
		//One flush runs at a time:  this one follows any that is running, and takes what is buffered when it starts, but
		//for a heartbeat made less than the publishing interval before, which waits for the next.
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

		//A page of the raw records the request asks for:  those of its nodes inside its range, from the day files the
		//range touches, a live file's runs merged by source time, and from the buffer, a running flush's records among
		//it.  At most Limit values, the bounds counted, but for a first page whose bounds alone pass it, which holds them,
		//and a last page's closing bounds, which follow its values.  The continuation is stateless:  where to resume by
		//time, how many records at that time each node has had, an archive's byte offset and generation, and a CRC of the
		//other arguments but Limit, so one passed with different nodes, times or bounds is refused.  A record that lands
		//behind the resume point between pages isn't in the rest of the read.  Throws for a request with neither time,
		//no nodes or a continuation that isn't this read's, a UAException with Bad_ContinuationPointInvalid, and when a file
		//the read opens can't be opened or read through.
		α Read( const ReadRequest& request, SRCE )ε->ReadResult;
		//Part 11's HistoryUpdate on the group's history (spec *Edits*), by a caller the host has checked.  The next flush
		//runs it, after writing the buffer, so every value it targets is in its day's file before it looks:  it writes a
		//Modification record for each value it changes to the modifications file of that value's day, one append and
		//checkpoint per day, fsynced, keeps each node's newest record and the later files' start values current, and
		//resumes with the result once the records are durable, so an edit a caller was told succeeded survives a crash.  A
		//day whose file can't be written fails that day's values with Bad_UnexpectedError, as does one the flush before
		//couldn't write, and the other days stand.  A range delete that reaches such a day answers Bad_UnexpectedError, as
		//does one when the days can't be listed.  One result per entry, in order; a node that isn't a member answers
		//Bad_NodeIdUnknown, a value with no SourceTimestamp, or one no day holds, Bad_InvalidTimestampArgument, and one no
		//file can hold the status it would be stored with.  Resumes with an exception for a group that was removed or
		//has stopped, or when the historian ends before the edit is written.
		α Edit( vector<EditDetails> details, Writer by, SRCE )ι->EditAwait;
		//When the earliest day that holds a file of the group starts, in timeZone:  OpcServer's StartOfArchive.  None while
		//it has no file.  Throws when hist.path can't be walked.
		α Earliest( SRCE )Ε->optional<TimePoint>;
	private:
		friend struct FlushAwait;
		friend struct EditAwait;
		friend struct Historian;
		friend struct Store;
		struct Editing;//an edit as the flush runs it (Edit.cpp).
		struct Job;//a write the flush does after its batch:  a correction's, or an edit's.
		//A value as Enqueue took it:  what it takes in a file, and whether a file can hold it.  Kept is the copy its node
		//keeps once it is stored, made before the lock unless the value goes over the node's last one in place.
		struct Arrival{ Value Data; uint32_t Bytes; bool Unsupported; bool NotUtf8; optional<Value> Kept; };
		struct Node{
			ExNodeId Id; Thresholds Config; optional<TimePoint> Break; bool Unsupported{}; bool NotUtf8{};
			//Its last stored value as collected, which the compression test compares and a heartbeat repeats, or the marker
			//stored since.  Marked for a marker:  nothing to repeat, and whatever comes next is stored.
			optional<Value> Stored;
			bool Marked{};
			optional<UA_DateTime> ValueTs;//the last stored value's primary time:  MinTimeInterval counts from it, and a heartbeat carries it.
			UA_DateTime RecordTs{ std::numeric_limits<UA_DateTime>::min() };//the newest primary time it stored, a heartbeat's or a marker's included.
			UA_DateTime KeptTs{ std::numeric_limits<UA_DateTime>::min() };//RecordTs, but for a heartbeat still buffered:  what dropping those leaves.
			TimePoint StoredAt{};//when it last stored a record, which MaxTimeInterval counts from.
			optional<Arrival> Pending;//what MinTimeInterval holds.
			TimePoint Due{};//when its interval ends, which a change of MinTimeInterval moves.
			uint Serial{};//its pending value's, which that one's timer names.
			optional<UA_DateTime> Delivered;//the SourceTimestamp of the last value it delivered, when that came with one.
			Duration Offset{};//arrival less SourceTimestamp, of the last change it delivered.
			bool Joined{ true };//no value since it joined.
			optional<UA_DateTime> Beat;//its newest heartbeat still buffered, as far as is known.
			optional<TimePoint> BeatAt;//when the heartbeat timer looks at it next:  its place in the group's queue.
			α Keep( UA_DateTime time )ι->void{ KeptTs = std::max( KeptTs, time ); RecordTs = std::max( RecordTs, time ); }//a record no drop takes back.
		};
		//Bytes is what the record takes in a file, near enough.  Copied is a membership change whose copies for later
		//days' files are made, or one of those copies.
		struct Buffered{ Record Item; uint Sequence; uint32_t Bytes; bool Copied{}; };
		Ω Cost( const Buffered& b )ι->uint{ return b.Bytes+sizeof(Buffered); }//what a buffered record counts against maxBuffer.
		//What a full buffer dropped of a node, until a flush marks it:  a Bad_DataLost at the earliest source time among
		//them, then the one with the latest, written back.
		//Newest is none once DropBeats took it, a heartbeat:  the marker then goes at its Sequence, and the gap ends at the
		//node's next value.
		struct Lost{ Value Marker{ (StatusCode)UA_STATUSCODE_BADDATALOST }; optional<Buffered> Newest; uint Count{}; uint Sequence{}; };

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
		//What a change made under _mutex leaves for once it is released:  what its pushes ask for, each timer it
		//replaced, and a warning, which is logged on the clock's hop, off the collection path.
		struct Effects{
			α operator+=( Pushing pushing )ι->void{ Pushed = { Pushed.Flush || pushing.Flush, Pushed.Trim || pushing.Trim }; }
			Pushing Pushed{};
			vector<IClock::TimerId> Stale;
			string Warning;
		};
		α Finish( Effects&& effects )ι->void;
		//Enqueue's, for a member's value:  the gap comparison for its first after a break, else the compression test.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Collect( NodeIndex index, Node& node, Arrival&& arrival, TimePoint now, Effects& effects )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α StoreValue( NodeIndex index, Node& node, Arrival&& arrival, TimePoint now, Effects& effects )ι->void;
		//A break's Bad_DataLost at `at`, found by the value ended.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Mark( NodeIndex index, Node& node, UA_DateTime at, const UA_DataValue& ended, TimePoint now, Effects& effects )ι->void;
		//The node's pending value, at its interval's end, a break, or the node's leaving:  stored if it still passes.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Settle( NodeIndex index, Node& node, TimePoint now, Effects& effects )ι->void;
		α Expire( NodeIndex index, uint serial )ι->void;//a pending value's timer.
		//That timer, for an interval that ends at due:  one it replaces finds nothing of its own.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α ExpireAt( NodeIndex index, Node& node, TimePoint due, TimePoint now )ι->void;
		//Drops the node's buffered heartbeats at or after `from`, the time of a change that passed:  each would replay
		//the old value over it.  With made, those the timer made at or after `from`, a break reported late:  each repeats
		//the value over a dead feed.  One the trim holds to write back goes as one in the buffer does.  The node's newest
		//record is then the newest that is left.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α DropBeats( NodeIndex index, Node& node, UA_DateTime from, bool made=false )ι->void;
		//Whether the heartbeat repeats the node's last value:  it has one, and neither a break nor a pending value is
		//waiting to say what follows it.
		Ω Beats( const Node& node )ι->bool{ return node.Config.MaxTimeInterval>Duration::zero() && node.Stored && !node.Marked && !node.Break && !node.Pending; }
		//The group's heartbeat timer, moved up to the node's deadline when that is the earliest.  The node waits in the
		//timer's queue for it, unless it already waits there for a sooner one.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Arm( NodeIndex index, Node& node, TimePoint now, Effects& effects )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Arm( TimePoint due, TimePoint now, Effects& effects )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Watch( NodeIndex index, Node& node, TimePoint due )ι->void;//queues the node for due.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α ScheduleBeat( TimePoint due, TimePoint now )ι->void;
		//That timer's:  a heartbeat for each node that stored nothing for its MaxTimeInterval, and the next deadline's timer.
		//It visits the nodes whose deadline has come, not the group.
		α Beat( uint serial )ι->void;
		α Schedule( Duration after )ι->IClock::TimerId;
		//The clock's flush `delay` after the next midnight in timeZone, by when the day's last flush has landed:  it
		//rewrites the day's file as its archive.
		α ScheduleMidnight()ι->IClock::TimerId;
		α Midnight()ι->void;//that timer's:  the next midnight's, and the flush.
		//The group's timers, which Disarm takes off it under _mutex, for Cancel to cancel outside it.
		struct Timers final{ IClock::TimerId Delay{}; IClock::TimerId Midnight{}; IClock::TimerId Beat{}; };
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Disarm()ι->Timers{ return { std::exchange(_timer, 0), std::exchange(_midnight, 0), std::exchange(_beat, 0) }; }
		α Cancel( Timers timers )ι->void;
		//After a flush that wrote all it took:  forgets each node that left whose values are all written, since no other
		//can arrive for it.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α PruneGone()ι->void;
		//A value flagged for the flush to warn of, dropped without being written back:  its node's next of its kind is
		//flagged instead, so the warning isn't lost with it.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Unflag( const DataValue& gone )ι->void;
		//Each node's lost values into y, as a marker and the one written back.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α LostRecords( vector<Buffered>& y )Ι->void;
		//LostRecords, emptying _lost.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α MarkLost( vector<Buffered>& y )ι->void;
		//The buffer, for a flush at taken:  each node's lost values first, as a marker and the one written back, then the
		//rest as they arrived.  A heartbeat made less than the publishing interval before stays for the next flush, so a
		//change sampled before it and still on its way finds it to drop:  none at the group's end, which no flush follows.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Take( TimePoint taken )ι->vector<Buffered>;
		//What a read sees of the buffer, the records of the nodes it wants:  what a running flush took and hasn't yet
		//written, then the buffer as the next Take has it, each node's lost values first.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Snapshot( absl::FunctionRef<bool( NodeIndex )> wanted )Ι->vector<Buffered>;
		//The flush's batch, as it stands, which Read serves until each record is in a file.  Set under _filesMutex, which a
		//read holds through its look at the buffer, so no record is in neither.  Shared, not copied:  the flush reads it
		//outside _mutex and changes it only under it.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Taking( sp<vector<Buffered>> batch )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Wrote( uint from, uint to )ι->void;//batch's [from, to) are in a file, or dropped.
		//What a flush couldn't write, held's records of its batch, back to the front of the buffer, without counting toward
		//the flush at 8 KB, followed by the marker and value of each gap dropped while it was out.  Moved out of the batch
		//under _mutex, so a read finds each in one place.  True as Push.
		α Return( vector<Buffered>& batch, vector<uint>&& held )ι->bool;
		//A Flush or a Settled awaiter's:  waits for the next flush to start and end, or for none to be running.
		α Request( FlushAwait& waiter )ι->void;
		//An edit's:  queued for the next flush, which is started when none is running.  Resumes it with an exception at once
		//for a group that was removed or has stopped.
		α Request( EditAwait& waiter )ι->void;
		//The flush's steps after its batch, each a list of writes it does in order:  the start-value corrections the batch's
		//records reach, then each edit's value files, its modifications and its corrections.  None once every step is done.
		α Step( uint step, vector<up<Editing>>& edits, const struct Membership& members, TimePoint taken, SL sl )ι->optional<vector<Job>>;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_filesMutex) α Corrections( const struct Membership& members, TimePoint taken, SL sl )ι->vector<Job>;
		α Creations( Editing& edit, const struct Membership& members, TimePoint taken, SL sl )ι->vector<Job>;
		α Modifications( Editing& edit, const struct Membership& members, TimePoint taken, SL sl )ι->vector<Job>;
		α Newest( Editing& edit, SL sl )ι->void;//each node's newest record as the edit leaves it.
		α Failed( vector<up<Editing>>& edits, string why )ι->void;//what the historian's end leaves unwritten.
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
		IClock::TimerId _beat ABSL_GUARDED_BY(_mutex){};//the heartbeat's, due at _beatDue.
		TimePoint _beatDue ABSL_GUARDED_BY(_mutex){};
		uint _beatSerial ABSL_GUARDED_BY(_mutex){};//the timer that counts:  one it replaced does nothing.
		//The nodes the heartbeat timer is to look at, soonest first, so that it visits only those whose time has come.  An
		//entry counts while it is its node's BeatAt.
		using Deadline = std::pair<TimePoint,NodeIndex>;
		std::priority_queue<Deadline,vector<Deadline>,std::greater<Deadline>> _beats ABSL_GUARDED_BY(_mutex);
		bool _ended ABSL_GUARDED_BY(_mutex){};//no flush starts.
		bool _flushing ABSL_GUARDED_BY(_mutex){};//a flush is running, the only one that changes _files.
		bool _again ABSL_GUARDED_BY(_mutex){};//the clock asked for another meanwhile.
		vector<FlushAwait*> _waiters ABSL_GUARDED_BY(_mutex);//each waits on the next flush to start.
		vector<FlushAwait*> _settling ABSL_GUARDED_BY(_mutex);//each waits for no flush to be running.
		vector<up<Editing>> _edits ABSL_GUARDED_BY(_mutex);//each for the next flush to run, in order.
		//In index order, so whatever walks it to write records writes them the same way every run, and a B-tree, so loading
		//and Enqueue's lookup stay logarithmic.  The maps by NodeId are hashed:  nothing walks them.
		absl::btree_map<NodeIndex,Node> _nodes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,NodeIndex> _indexes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ExNodeId,TimePoint> _left ABSL_GUARDED_BY(_mutex);//each removed node's break, which a rejoin takes.
		//Each index that left, which a file holding a value of it may yet need to map, until those values are written.
		absl::flat_hash_map<NodeIndex,ExNodeId> _gone ABSL_GUARDED_BY(_mutex);
		NodeIndex _nextIndex ABSL_GUARDED_BY(_mutex){ 1 };
		optional<TimePoint> _down ABSL_GUARDED_BY(_mutex);//when the connection broke, while it is down.
		bool _closed ABSL_GUARDED_BY(_mutex){};
		bool _archived ABSL_GUARDED_BY(_mutex){};//as the last flush left the group's files:  none live.
		bool _stopped ABSL_GUARDED_BY(_mutex){};
		//The buffer, each part in the order it arrived:  membership changes, which are never dropped, and values.
		vector<Buffered> _changes ABSL_GUARDED_BY(_mutex);
		std::deque<Buffered> _values ABSL_GUARDED_BY(_mutex);
		//What the running flush took, in its order, until Return or the flush's end, and which of them it has written.
		sp<vector<Buffered>> _taken ABSL_GUARDED_BY(_mutex);
		vector<bool> _written ABSL_GUARDED_BY(_mutex);
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
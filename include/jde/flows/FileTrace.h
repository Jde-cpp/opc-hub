#pragma once
#include <jde/fwk/io/records/Days.h>
#include "Trace.h"

//The trace's day files (docs/flows/spec.md *Trace*, *History*):  a flow's records under `<root>/<slug>/<yyyy>/<m>/<d>/trace.binpb`,
//the record layer's files as the historian's (libs/flows/src/io/Records.h), and the reads over them.
namespace Jde::Opc::Flows{
	using Day = IO::Records::Day;
	constexpr sv TraceFileName{ "trace.binpb" };

	struct TraceSettings final{
		fs::path Root;//the flows' directory:  one per flow under it, by its slug.
		Duration Delay{ std::chrono::minutes{1} };//how long a record waits in a flow's buffer before its flush.
		Duration Retention{ std::chrono::days{30} };//days older than this go at the daily roll.
		uint MaxSize{ 10ull<<30 };//bytes over every flow's days, past which the oldest go at the roll, 10 GiB.
		uint ValueLimit{ 4096 };//bytes of a message's json a hop record keeps;  past it the json is cut, and the record says so.  0 keeps it whole.
		ETraceLevel Level{ ETraceLevel::Hops };//what a flow takes when its row names none.
		const std::chrono::time_zone* Zone{};//the days' zone;  none for the host's own.
	};

	//A message's json as a record keeps it:  cut at `limit` bytes, on a UTF-8 boundary, so what is kept is text;  0 keeps it whole.
	α CutJson( sv json, uint limit )ι->sv;

	//Where a page ended, the opaque continuation's:  the last record's time and how many at that time the page returned,
	//counting those its query matches in file order, so a page that ends inside a tie takes up where it left.
	struct TraceCursor final{ TimePoint Time; uint32 Skip{}; };
	//flowTrace's (spec *History*):  the records in a range, oldest first from Start, or newest first back from End with no
	//Start, or in their order with both;  each bound inclusive;  the live trace's or one replay's, narrowed to a node, a
	//message or a kind.
	struct TraceQuery final{
		optional<TimePoint> Start, End;
		optional<string> Node;
		optional<MessageId> Msg;//the records of the message itself.
		optional<ERecordKind> Kind;
		uint32 Run{};//0, the live trace;  a replay's, by its id.
		uint Limit{ 10'000 };//the most records a page holds.
		optional<TraceCursor> From;//the page before's end.
		α Reverse()Ι->bool{ return Start && End ? *Start>*End : !Start; }
	};
	struct TracePage final{
		vector<TraceRecord> Records;
		optional<TraceCursor> Next;//none on the last page.
	};
	//flowMessage's:  a message's lineage, the records of its ancestors up to the source, its own and its descendants', in
	//time order.  More:  the limit cut it short.
	struct TraceLineage final{ vector<TraceRecord> Records; bool More{}; };
	//flowCounts's:  one node's figures in one bucket.
	struct CountRow final{ TimePoint Time; NodeCounts Counts; };
	//A page of them, whole buckets:  Next, the start of the bucket the next page begins with, none on the last.
	struct CountPage final{ vector<CountRow> Rows; optional<TimePoint> Next; };

	struct FileTrace;
	//The flows' trace files under Root, one TraceStore a host:  each flow's buffer, flushed Delay after its first record
	//through the record layer's appender into the day file of each record's time, and read back merging the day's runs;
	//the daily roll, on the host's clock, never a run's, which walks every flow's days newest first and removes the oldest
	//past Retention or MaxSize over them all;  and a flow's purge, which takes its directory.  A flow's records are written
	//by one flush at a time, so what is known of its files is the process's own appends and each file's first-open scan.
	//Reads are synchronous, positioned reads of the files, so a caller on an executor thread never waits on the executor;  a
	//range of days is a long read, which a host runs off its executor.  A read under way when the store stops throws.
	struct TraceStore final : std::enable_shared_from_this<TraceStore>, noncopyable{
		//clock:  the host's, which the flushes and the roll are scheduled on.
		TraceStore( TraceSettings settings, sp<Hist::IClock> clock )ι;
		~TraceStore();
		α Start()ι->void;//the roll, now and at each midnight of the days' zone.
		//The timers cancelled, and every flow's buffer written, waiting on the writes for up to `wait`.  Before the executor
		//ends.  Stop again, or a Start after, is nothing.
		α Stop( Duration wait=std::chrono::seconds{10} )ι->void;
		//A flow's trace, at `level`, or Level when the row names none:  what its Flow::Config takes.  `clock`:  the flow's,
		//which the records' times are on.  `tap`:  given each record as the trace makes it, whole, on the flow's strand,
		//before it goes to the buffer:  the live feed's (spec *Live*).  `run`:  a replay's id, on each record and each
		//minute's figures it writes, into the flow's files at the stream's times (spec *Replay*);  0 live.
		using Tap = function<void( const TraceRecord& )>;
		α Trace( sv slug, optional<ETraceLevel> level, sp<Hist::IClock> clock, Tap tap={}, uint32 run=0 )ι->sp<FileTrace>;
		//The flow's buffer written now, rather than at Delay:  `done` once it is, or at once when there is nothing to write;
		//for a write that fails, once the records are back in the buffer.
		α Flush( sv slug, absl::AnyInvocable<void()> done={} )ι->void;
		α Buffered( sv slug )Ι->uint;//the bytes of the flow's buffer:  what a replay holds itself back by, flushing past a share of MaxBuffer.
		α Failing( sv slug )Ι->optional<string>;//why the flow's last write failed, until one succeeds.
		static constexpr uint MaxBuffer{ 16u<<20 };//bytes a flow's buffer holds:  past it a healthy file's flush comes now, and while the file is unwritable or behind the oldest go.
		α Roll()ι->void;//the daily roll, now:  the midnight roll Start set is left as it is.
		α Purge( sv slug )ι->void;//the flow's directory and what is known of its files;  its buffer dropped.
		//A flow's directory under `root` with no store, a purge's after the store stopped:  a slug that is no directory name
		//is refused, so none is followed out of root.
		Ω Remove( const fs::path& root, sv slug )ι->void;
		α Days( sv slug, SRCE )ε->vector<Day>;//the days that hold a file of the flow, oldest first.
		α Read( sv slug, const TraceQuery& query, SRCE )ε->TracePage;
		//The message's lineage over the days from `start`, or the day of the start its id carries, the gateway's or a
		//replay's range's, to `end`, or the present:  at most `limit` records, oldest first.  A message's descendants come
		//out of one pass over the days, each born after its parent's record of it;  each ancestor costs a pass of its own,
		//which ends at the first record of the message made from it:  what the ancestor did after is a sibling's path.
		//`run`:  the trace the message is of, the live one or a replay's, whose ids are its own.
		α Lineage( sv slug, MessageId id, optional<TimePoint> start, optional<TimePoint> end, uint limit, uint32 run=0, SRCE )ε->TraceLineage;
		//The minute records from `start`, or a page's `from`, to before `end`, summed per node into buckets `by` wide from
		//`start`, a minute at least:  rows by bucket, then by the node's first appearance in it.  A page holds whole buckets,
		//at most `limit` rows unless its one bucket has more;  0 for no limit.  `run`:  the live trace's figures, or a replay's.
		α Counts( sv slug, TimePoint start, TimePoint end, Duration by, uint limit=0, optional<TimePoint> from={}, uint32 run=0, SRCE )ε->CountPage;
		α Settings()Ι->const TraceSettings&;
		α Zone()Ι->const std::chrono::time_zone&;
		struct Files;//one flow's, the implementation's:  its buffer and what is known of its files.
		struct Impl;
	private:
		α Rolling()ι->void;//the roll's timer:  the roll, and the next midnight's set.
		up<Impl> _impl;
		friend struct FileTrace;
	};

	//A flow's trace over its store:  HopTrace's fold, each record into the flow's buffer.  Made by TraceStore::Trace.
	struct FileTrace final : HopTrace{
		FileTrace( sp<TraceStore> store, sp<TraceStore::Files> files, ETraceLevel level, sp<Hist::IClock> clock, TraceStore::Tap tap={}, uint32 run=0 )ι;
		~FileTrace();
	protected:
		α Write( TraceRecord&& record )ι->void override;
		α WriteMinute( const MinuteCounts& minute )ι->void override;
	private:
		sp<TraceStore> _store;
		sp<TraceStore::Files> _files;
		TraceStore::Tap _tap;
		const uint32 _run;
	};
}
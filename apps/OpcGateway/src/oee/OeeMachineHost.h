#pragma once
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include "OeeRows.h"

namespace Jde::Opc::Gateway{
	struct HistGroupHost;
	//One oee_machines row as the gateway runs it (docs/oee/spec.md *The engine*):  the library's engine, its subscription to the
	//machine's group, and the writer its sink queues to.  Start is the restart:  the rows a run before this one wrote past
	//`processed` go, the stop over it is loaded as the machine's state, the snapshot is paged into the engine from one tick
	//before `processed`, and the engine goes live.  The group delivers on the thread that stored the record, so OnRecords only
	//hands the records to the machine's strand, where the engine takes them;  what the engine tells its sink is queued under the
	//engine's lock and written on the executor, one batch at a time, each statement awaited.  A write that fails ends the
	//machine and tells the host, which starts it again from `processed`:  the group's history is what the rows are made from.
	//The group owns this through its registration, and this the subscription, which holds the registration weakly.
	//The rebuilds (spec *The engine*, *Intervals*):  a record in a written interval, late or a Modification, a marker or an idle
	//stop asks the sink for one over a span, and recomputeOee asks for one on demand.  They run here one at a time, after the
	//writes and the start, each span narrowed to its node's next stored record and overlapping ones merged.  A span that reaches
	//the open interval, `processed`, one on a machine whose count is its state, whose idle stops the clock finds, or one whose
	//end falls inside the stop in progress hands the host a takeover:  the machine starts again from the first row the span
	//touches, this engine's source offset handed to the next, so the live state is re-derived from what it read.  Any other is
	//rebuilt in place:  the interval rows it touches, extended through any stop that spans their end so the fresh engine ends on
	//running time, are read again from the group with the bound before, run through an engine made at the widths they had and
	//ended at their end, and the rows over them deleted and written again, the stop that spans their start ended as the replay ends it.
	struct OeeMachineHost final : Hist::ISubscriber, std::enable_shared_from_this<OeeMachineHost>{
		struct Settings final{ Duration Settle; uint ReadLimit; };
		//What the engine is made from:  a change of any is a restart, where a width's is SetWidth.
		struct Inputs final{ vector<Oee::Signal> Signals; optional<Duration> IdleTimeout; Duration SmallStop{}; };
		using Failed = function<void( OeeMachinePK machine, const OeeMachineHost* host, string why )>;
		//A rebuild that is a restart:  from `from`, `offset` this engine's source offset for the next's Live, `why` for the log.
		using Takeover = function<void( OeeMachinePK machine, const OeeMachineHost* host, TimePoint from, Duration offset, string why )>;
		struct Writer;
		//What a QuietAwait waits for, as bits.
		enum EWait : uint8{ ForWrites=1, ForStart=2, ForRebuild=4, ForAll=7 };
		struct QuietAwait final : AnyVoidAwait{
			QuietAwait( sp<Writer> writer, uint8 of, SL sl )ι:AnyVoidAwait{ sl }, For{ of }, _writer{ move(writer) }{}
			α await_ready()ι->bool override;
			const uint8 For;//EWait bits:  Starting's own wait is for the writes alone, and a rebuild's for the writes and the start.
		protected:
			α Suspend()ι->void override;
		private:
			sp<Writer> _writer;
		};
		//`offset`:  the source offset the engine goes live with, a takeover's from the engine before it, else none (reviews-issue/400-oee-tables.md #4).
		OeeMachineHost( OeeMachineRow row, Inputs inputs, sp<HistGroupHost> group, sp<Hist::IClock> clock, Settings settings, Duration offset={} )ι;
		~OeeMachineHost();
		α Id()Ι->OeeMachinePK{ return _row.Id; }
		α Made()Ι->const Inputs&{ return _inputs; }
		//On the executor, detached.  `failed` hears a start, a write or a rebuild that failed, after this has ended;  `takeover` a
		//rebuild that is a restart, which ends this.
		α Start( Failed failed, Takeover takeover )ι->void;
		//Nothing more is taken, told or queued, and the write or rebuild statement in flight is the last:  a statement already out finishes.
		α End()ι->void;
		α Quiet( SRCE )ι->QuietAwait;//resumes once no write or rebuild is running and Starting has returned, on the thread that ended the last.
		α Idle()Ι->bool;//nothing queued, no write running and no rebuild asked or running - for tests and diagnostics.
		α Live()Ι->bool{ return _live; }//the snapshot is replayed and the engine runs on the clock.
		α State()Ι->optional<Oee::LiveState>;//none before the engine is made, and after the end.
		α SetWidth( Duration width )ε->void;//the machine's `width` changed:  Engine::SetWidth.
		α Recompute( Oee::Span span )ι->void;//recomputeOee's:  a rebuild over the span, as the engine asks for one.
		α Rebuilds()Ι->uint;//the rebuilds asked for, the engine's and recomputeOee's - for tests and diagnostics.

		α OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void override;
		α OnEnded( Hist::ESubscriptionEnd why )ι->void override;
	private:
		struct Rebuilt;//the sink a rebuild's engine tells, which only collects.
		α Starting()ι->VoidTask;
		α Subscribe( UA_DateTime from )ε->Hist::ReadResult;//registers, and the snapshot's first page, which opens with the bounds.
		α Next()ε->Hist::ReadResult;
		α Written( SRCE )ι->QuietAwait;//Quiet, but for Starting and a rebuild.
		α Rebuilding()ι->VoidTask;//the queued asks, one span at a time, until none is left or a takeover.
		α Current()Ι->sp<Oee::Engine>;//the engine, or none before it is made or after the end.

		const OeeMachineRow _row;
		const Inputs _inputs;
		const sp<HistGroupHost> _group;
		const sp<Hist::IClock> _clock;
		const Settings _settings;
		const Duration _offset;
		const sp<Writer> _writer;
		boost::asio::strand<boost::asio::io_context::executor_type> _strand;
		std::atomic<bool> _ended{}, _live{};
		Failed _failed;//Start's, before anything runs.
		Takeover _takeover;
		std::mutex _pageMutex;//the subscription's:  a page's read and the end.
		up<Hist::Subscription> _subscription;
		mutable std::mutex _queueMutex;//the engine and what arrived before it:  never held over a page.
		sp<Oee::Engine> _engine;
		vector<Hist::Proto::HistoryRecord> _early;
		Duration _width;//an edit while the engine is being made is its to apply.
	};
}
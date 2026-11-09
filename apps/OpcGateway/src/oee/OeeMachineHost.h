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
	struct OeeMachineHost final : Hist::ISubscriber, std::enable_shared_from_this<OeeMachineHost>{
		struct Settings final{ Duration Settle; uint ReadLimit; };
		//What the engine is made from:  a change of any is a restart, where a width's is SetWidth.
		struct Inputs final{ vector<Oee::Signal> Signals; optional<Duration> IdleTimeout; Duration SmallStop{}; };
		using Failed = function<void( OeeMachinePK machine, const OeeMachineHost* host, string why )>;
		struct Writer;
		struct QuietAwait final : AnyVoidAwait{
			QuietAwait( sp<Writer> writer, SL sl )ι:AnyVoidAwait{ sl }, _writer{ move(writer) }{}
			α await_ready()ι->bool override;
		protected:
			α Suspend()ι->void override;
		private:
			sp<Writer> _writer;
		};
		OeeMachineHost( OeeMachineRow row, Inputs inputs, sp<HistGroupHost> group, sp<Hist::IClock> clock, Settings settings )ι;
		~OeeMachineHost();
		α Id()Ι->OeeMachinePK{ return _row.Id; }
		α Made()Ι->const Inputs&{ return _inputs; }
		//On the executor, detached.  `failed` hears a start or a write that failed, after this has ended.
		α Start( Failed failed )ι->void;
		//Nothing more is taken, told or queued, and the write in flight is the last:  a statement already out finishes.
		α End()ι->void;
		α Quiet( SRCE )ι->QuietAwait;//resumes once no write is running, on the thread that ended the last.
		α Idle()Ι->bool;//nothing queued and no write running - for tests and diagnostics.
		α Live()Ι->bool{ return _live; }//the snapshot is replayed and the engine runs on the clock.
		α State()Ι->optional<Oee::LiveState>;//none before the engine is made, and after the end.
		α SetWidth( Duration width )ε->void;//the machine's `width` changed:  Engine::SetWidth.
		α Rebuilds()Ι->uint;//the rebuilds the engine has asked for, which nothing runs yet (#401) - for tests and diagnostics.

		α OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void override;
		α OnEnded( Hist::ESubscriptionEnd why )ι->void override;
	private:
		α Starting( Failed failed )ι->VoidTask;
		α Subscribe( UA_DateTime from )ε->Hist::ReadResult;//registers, and the snapshot's first page, which opens with the bounds.
		α Next()ε->Hist::ReadResult;

		const OeeMachineRow _row;
		const Inputs _inputs;
		const sp<HistGroupHost> _group;
		const sp<Hist::IClock> _clock;
		const Settings _settings;
		const sp<Writer> _writer;
		boost::asio::strand<boost::asio::io_context::executor_type> _strand;
		std::atomic<bool> _ended{}, _live{};
		std::mutex _pageMutex;//the subscription's:  a page's read and the end.
		up<Hist::Subscription> _subscription;
		mutable std::mutex _queueMutex;//the engine and what arrived before it:  never held over a page.
		sp<Oee::Engine> _engine;
		vector<Hist::Proto::HistoryRecord> _early;
	};
}
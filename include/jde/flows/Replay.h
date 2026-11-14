#pragma once
#include "Flow.h"

//A replay (docs/flows/spec.md *Replay*):  a flow's graph run over its sources' records from one time to another, on the
//stream's clock, with no gateway in it.
namespace Jde::Opc::Flows{
	//A run's clock, the stream's.  It stands where the run last put it, at a record's time as the record is delivered, and
	//moves only as the run moves it:  AdvanceTo runs each timer due on the way, in due order and ties in the order
	//scheduled, with Now() at the timer's own time, so a timer a callback schedules inside the span runs too, the manual
	//clock's rule (libs/historian/tests/ManualClock.h).  An interval is due at Now()+after.  Nothing here reads the wall
	//clock, so the same records move it the same way twice.
	struct StreamClock final : Hist::IClock{
		explicit StreamClock( TimePoint start )ι:_now{ start }{}
		α Now()Ι->TimePoint override;
		α Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId override;
		α Schedule( Duration after, absl::AnyInvocable<void()> f )ι->TimerId override;
		α Cancel( TimerId id )ι->bool override;
		//The timers due at or before `to`, `most` of them at the most:  true with none left before `to`, where the clock
		//then stands;  false with more due, the clock at the last it ran.  What escapes a timer is logged, as the system
		//clock's is.  The run's, on its strand, where a flow's timers then run inline.
		α AdvanceTo( TimePoint to, uint most=std::numeric_limits<uint>::max() )ι->bool;
		α Pending()Ι->uint;
	private:
		mutable std::mutex _mutex;
		TimePoint _now;
		TimerId _nextId{};
		std::map<std::pair<TimePoint,TimerId>,absl::AnyInvocable<void()>> _timers;
	};

	//A source's record in a run:  when it was, by its source, and the fields of the message its node sends for it live,
	//`topic`, `payload` and a data change's.  Its `_id` and its `_time`, the clock's, are the run's to give.
	struct StreamRecord final{ TimePoint Time; jobject Fields; };
	//One source node's records over a run's range, oldest first, a page at a time:  no record is the end.  On the run's
	//strand, and what it throws fails the run.
	struct IStream{
		virtual ~IStream()=default;
		β Next()ε->vector<StreamRecord> =0;
	};
	//A run's host:  a flow's (IHost), and the records.
	//  Stream:  an `opc in`'s and a `group in`'s records over the range, which the run delivers in the node's place;  what
	//  it throws refuses the run, as the node's making refuses a flow.
	//  Source:  a `timer` and a `calendar`, made as they are live, which tick on the run's clock.  A `connection` is the
	//  run's own, and silent, as an `inject` and a `link in` are, which nothing feeds:  nothing live reaches the stream's clock.
	//  Sink:  each a capture (INode::Capture), but what the run's mode writes.
	struct IReplayHost : IHost{
		β Stream( const NodeDef& node )ε->up<IStream> =0;
	};

	enum class ERunEnd : uint8{ Completed, Cancelled, Failed };
	α ToString( ERunEnd end )ι->sv;//"completed", "cancelled", "failed".

	//A flow's graph run once over its sources' records (spec *Replay*), on a strand, a sandbox and a clock of its own.  The
	//sources' pages are merged by time into one stream, a tie going to the node first in the graph.  For each record the
	//clock is moved to its time, the timers due before it run on the way, each with what it sends through the flow before
	//the next, the record is delivered as its node's message, and the next waits until no message is queued at a node:  so
	//a delay, a window's close and a body's setTimeout fire at stream times in the order they would have live, and the same
	//records give the same trace twice.  After the last record the clock runs on to To, and the flow stops, what its nodes
	//still hold dropped and counted.
	struct Run final{
		struct Config final{
			sp<StreamClock> Clock;//the run's, standing at From;  null:  its own.  What the run's trace times its records on.
			sp<IReplayHost> Host;
			TimePoint From, To;//the range, each bound inside it.
			//How the run ended, once, after its flow's teardown:  on its strand, or on Start's thread for a run whose nodes
			//could not be made.  `why`:  a failure's, a node's refusal, a start that threw or a page that could not be read.
			function<void( ERunEnd end, string why )> Ended;
			//Between two steps, once what the last sent is through the flow:  the run goes on when `next` is called, from
			//any thread.  How a host holds a run back while its trace is written.  None:  at once.
			function<void( absl::AnyInvocable<void()> next )> Pace;
		};
		//`flow`:  what the graph runs with, its queue, its sandbox's limits, its trace and its context, a scratch copy.  Its
		//Clock and Host are the run's own, and its ids, without any, count from From, so two runs of a range have the same.
		Run( Graph graph, Flow::Config flow, Config config )ι;
		~Run();//cancels it.
		Run( Run&& )ι=default;
		//Makes the nodes and starts.  False for a run that could not, which Ended tells, Failed with the fault, before this returns or after.
		α Start()ι->bool;
		α Cancel()ι->void;//any thread:  the flow stops, and Ended tells Cancelled, unless it was ending already.
		α Delivered()Ι->uint;//the records delivered.
		α Pages()Ι->uint;//the pages its streams answered, the empty last ones aside.
		α Counts()Ι->vector<NodeCounts>;//the flow's.
		α Fault()Ι->optional<Flows::Fault>;
	private:
		struct Impl;
		sp<Impl> _impl;
	};
}
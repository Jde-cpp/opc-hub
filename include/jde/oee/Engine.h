#pragma once
#include <jde/historian/Group.h>
#include "types.h"

//The engine of docs/oee/spec.md *The engine*:  one per machine, over the records of its historian group, with no gateway
//and no database.  A host subscribes to the group, hands the engine the snapshot and then the stream, and writes what the
//sink is told.
namespace Jde::Opc::Oee{
	struct EngineConfig final{
		vector<Signal> Signals;
		Duration SmallStop{};//for the live state alone:  a read classes a stop.
		optional<Duration> IdleTimeout;//a machine with no State signal stops once this passes with no count.
		Duration Width{ minutes{1} };//of its rows.
		Duration Settle{ seconds{5} };//how long after an interval ends its row is written.
	};
	//Where a run starts.  Bounds are each signal's value before From, as a subscription's snapshot and a bounded read open
	//with them:  the stepped state and the counters' last values.  A signal with none, or Bad_BoundNotFound, starts unknown,
	//and a bound after From, the read's closing one, is skipped.
	//Open is the row a restart found with no end;  where the State bound says otherwise the bound wins, and the row ends at From.
	struct Opening final{
		TimePoint From;//the machine's `processed`:  the first row starts here and runs to the next boundary of the width.
		vector<Hist::Proto::DataValue> Bounds;
		optional<Stop> Open;
	};
	//What a tile shows, and what the shift's figures add to the stored rows.
	struct LiveState final{
		EState State{ EState::Unknown };
		TimePoint Since;//when it began:  the opening at the earliest, but for a stop a restart found open.
		optional<Stop> Open;//the stop, or the span of unknown time, in progress.
		bool Small{};//a stop in progress that is still shorter than SmallStop.
		optional<TimePoint> LastCount;
		TimePoint Processed;//the end of the last row written.
		TimePoint At;//the engine's time:  how far Rows run.
		Duration Offset{};//the source offset:  what a host hands the engine that takes over after a rebuild.
		vector<Interval> Rows;//the rows not yet written, the last still filling, its Width the time it holds so far.
	};
	//Called under the engine's lock, in order, so an implementation only queues or posts and never calls the engine.
	struct ISink{
		virtual ~ISink()=default;
		β Closed( const Interval& row )ι->void=0;//an interval's row, once:  a record in it from here on is a Rebuild.
		//A stop or a span of unknown time began:  its row.  End is set when it has ended too, a replay's or one a batch both began and ended.
		β Opened( const Stop& stop )ι->void=0;
		β Closed( const Stop& stop )ι->void=0;//one the sink was told had begun ended:  the row at Start, as it now reads.
		//The rows that start at or after `from` are these and no others:  an open stop's code learned, an idle stop a marker
		//takes back, a record out of order.
		β Rewritten( TimePoint from, const vector<Stop>& stops )ι->void=0;
		//A record of `node` in a written interval, late or a Modification, or an idle stop that began in one:  the rows over
		//`span` are to be rebuilt from the group's history.  The span ends no later than the node's next record:  at the newest
		//of its records the engine has let go, else at its next the engine holds, which the host narrows to the next one stored.
		//With neither it ends at the engine's time:  it reaches the open interval, and a fresh engine takes over.
		β Rebuild( uint32 node, Span span )ι->void=0;
		β Live( const LiveState& state )ι->void=0;//the state, the row in progress or Small changed.
	};

	//Synchronized:  the stream, the clock's timers and the host may each call from a thread of their own.  The records are
	//taken in any order:  one at or after Processed is placed by its source time, the open intervals tallied again from the
	//minute before it, and an earlier one asks for a Rebuild.  One stamped more than a minute ahead of the engine's clock, a
	//PLC's unset clock, waits for the clock to reach it.
	struct Engine final : noncopyable{
		Engine( EngineConfig config, Opening opening, sp<ISink> sink, sp<Hist::IClock> clock, SRCE )ε;//throws what ValidateSignals and ValidateWidth do.
		~Engine();
		//A page of the snapshot, in source-time order.  The stream is the clock:  an interval closes once a record past its
		//end is seen.  A value flagged Bound is skipped, the opening having taken it, and so is a heartbeat.
		α Replay( const vector<Hist::ReadValue>& page )ι->void;
		//The snapshot's end.  From here the clock is the wall clock less the source offset, so a server whose clock lags is not
		//late by the lag:  `offset` to begin with, what the historian keeps for the signals' nodes when the host has it, then
		//the least arrival less source time of the last few Good values the stream delivered, one more than a minute from it
		//believed once the next agrees.  An interval closes once that clock passes its end by Settle, and the idle timeout and
		//SmallStop are timers.  Applies what Records held.
		α Live( Duration offset={} )ι->void;
		//The end of a rebuild's run, which never goes live:  the stream's clock moves to `end`, closing each interval up to it.
		α Finish( TimePoint end )ι->void;
		//The stream:  ISubscriber::OnRecords'.  Held until Live.  A heartbeat is skipped and a Modification placed by the
		//time it targets.
		α Records( vector<Hist::Proto::HistoryRecord>&& records )ι->void;
		//The machine's `interval` changed:  in force from the first end of an interval, the open one's or one after, that is
		//a boundary of the new width too.  Throws what ValidateWidth does.
		α SetWidth( Duration width, SRCE )ε->void;
		α End()ι->void;//nothing more is taken or told:  the timers cancelled.
		α State()Ι->LiveState;
		α Processed()Ι->TimePoint;
	private:
		struct Impl;
		sp<Impl> _impl;//shared, so a timer holds it weakly.
	};
}
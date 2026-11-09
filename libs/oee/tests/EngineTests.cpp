//The engine over scripted record streams (docs/oee/spec.md *The engine*, *State*, *Stops*, *Counts*, *Intervals*):  a
//backfill replayed with the stream as its clock, and the stream live on a manual clock, against the rows and the stops a
//host would write.
#include "helpers.h"

namespace Jde::Opc::Oee::Tests{
	struct Streams : EngineTest{
		//State and Total at a minute's rows, running at 06:00 with the count at 100, a five-minute small stop.
		α Counting( optional<Stop> open={} )ε->Engine&{
			return Open( {Signals({{ERole::State, StateNode}, {ERole::Total, TotalNode}}), 5min}, {At(6), {Flag(StateNode, At(5,50), true), Count(TotalNode, At(5,59), 100)}, move(open)} );
		}
		//A count alone, whose counts are its state:  the last at 05:59:55.
		α CountOnly( Duration idle, std::initializer_list<std::pair<ERole,uint32>> more={} )ε->Engine&{
			EngineConfig config{ Signals({{ERole::Total, TotalNode}}) };
			for( let& [role, node] : more )
				config.Signals.push_back( {role, node} );
			config.IdleTimeout = idle;
			return Open( move(config), {At(6), {Count(TotalNode, At(5,59,55), 100)}} );
		}
		α Row( int minute )Ι->Interval{ return Sink->Row( At(6, minute) ); }
		Ω Minute( int minute, Duration run, Duration stop={}, Duration unknown={}, double total={} )ι->Interval{ return { At(6, minute), 1min, run, stop, unknown, total, total, 0 }; }
		using Rebuilt = std::pair<uint32,Span>;
		using Table = vector<string>;
	};

	TEST_F( Streams, ShiftOfStateChangesAndCounts ){
		auto& engine = Counting();
		Replay( {
			Count(TotalNode, At(6,0,30), 110), Flag(StateNode, At(6,2,15), false), Count(TotalNode, At(6,2,20), 112),
			Flag(StateNode, At(6,4,45), true), Count(TotalNode, At(6,5,30), 150)} );
		EXPECT_EQ( Sink->Rows.size(), 5u ) << "a row closes once a record past its end is seen";
		engine.Finish( At(6,10) );
		ASSERT_EQ( Sink->Rows.size(), 10u );
		EXPECT_EQ( Row(0), Minute(0, 1min, {}, {}, 10) );
		EXPECT_EQ( Row(1), Minute(1, 1min) );
		EXPECT_EQ( Row(2), Minute(2, 15s, 45s, {}, 2) );
		EXPECT_EQ( Row(3), Minute(3, {}, 1min) );
		EXPECT_EQ( Row(4), Minute(4, 15s, 45s) );
		EXPECT_EQ( Row(5), Minute(5, 1min, {}, {}, 38) );
		EXPECT_EQ( Row(9), Minute(9, 1min) );
		EXPECT_EQ( Sink->Table(), Table{"06:02:15-06:04:45"} );
		EXPECT_EQ( Sink->Calls, Table{"opened 06:02:15-06:04:45"} ) << "a replay's stop comes whole";
		EXPECT_TP( engine.Processed(), At(6,10) );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
		let state = engine.State();
		EXPECT_EQ( state.State, EState::Running );
		EXPECT_TP( state.Since, At(6,4,45) );
		EXPECT_TP( *state.LastCount, At(6,5,30) );
		EXPECT_TRUE( state.Rows.empty() );
	}

	//Unknown from the Bad record to the next Good one.  A gap ends a stop where it starts, a Good record after it that
	//reads stopped starts another at its own time, and the count across it lands where the gap ends.
	TEST_F( Streams, GapAndTheCountAcrossIt ){
		auto& engine = Counting();
		Replay( {
			Flag(StateNode, At(6,1), false),
			Bad(StateNode, At(6,2)), Bad(TotalNode, At(6,2)),
			Flag(StateNode, At(6,5,30), false), Count(TotalNode, At(6,5,40), 160),
			Flag(StateNode, At(6,7), true)} );
		engine.Finish( At(6,8) );
		EXPECT_EQ( Sink->Table(), (Table{"06:01:00-06:02:00", "06:02:00-06:05:30?", "06:05:30-06:07:00"}) );
		EXPECT_EQ( Row(1), Minute(1, {}, 1min) );
		EXPECT_EQ( Row(2), Minute(2, {}, {}, 1min) );
		EXPECT_EQ( Row(4), Minute(4, {}, {}, 1min) );
		EXPECT_EQ( Row(5), Minute(5, {}, 30s, 30s, 60) );
		EXPECT_EQ( Row(6), Minute(6, {}, 1min) );
		EXPECT_EQ( Row(7), Minute(7, 1min) );
	}
	//Uncertain is Bad, as the aggregates' default has it.
	TEST_F( Streams, UncertainIsUnknown ){
		auto& engine = Counting();
		auto uncertain = Flag( StateNode, At(6,1), true );
		uncertain.set_status( UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE );
		Replay( {uncertain, Flag(StateNode, At(6,1,30), true)} );
		engine.Finish( At(6,2) );
		EXPECT_EQ( Sink->Table(), Table{"06:01:00-06:01:30?"} );
		EXPECT_EQ( Row(1), Minute(1, 30s, {}, 30s) );
	}

	//A value below the last is a reset and counts whole;  a downward correction counts the new value over again.
	TEST_F( Streams, ResetAndDownwardCorrection ){
		auto& engine = Counting();
		Replay( {Count(TotalNode, At(6,0,10), 130), Count(TotalNode, At(6,1,10), 5), Count(TotalNode, At(6,2,10), 4), Count(TotalNode, At(6,3,10), 10)} );
		engine.Finish( At(6,4) );
		EXPECT_EQ( Row(0).Total, 30 );
		EXPECT_EQ( Row(1).Total, 5 );
		EXPECT_EQ( Row(2).Total, 4 );
		EXPECT_EQ( Row(3).Total, 6 );
	}

	//A heartbeat repeats a value and changes nothing:  with no State signal it is not a count, so the idle stop starts at the count it repeats.
	TEST_F( Streams, HeartbeatIsNoCount ){
		auto& engine = CountOnly( 1min );
		let count = Count( TotalNode, At(6,0,10), 101 );
		Replay( {count, Beat(count, At(6,0,50)), Count(TotalNode, At(6,3), 105), Count(TotalNode, At(6,3,30), 106)} );
		engine.Finish( At(6,4) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:10-06:03:00"} );
		EXPECT_EQ( Row(0), Minute(0, 10s, 50s, {}, 1) );
		EXPECT_EQ( Row(2), Minute(2, {}, 1min) );
		EXPECT_EQ( Row(3), Minute(3, 1min, {}, {}, 5) );
	}

	//Good is the Good signal's, else the total less the rejects, and the total without a Total signal the two summed.
	TEST_F( Streams, GoodAndRejectCounts ){
		Open( {Signals({{ERole::State, StateNode}, {ERole::Total, TotalNode}, {ERole::Reject, RejectNode}})}, {At(6), {Flag(StateNode, At(5), true), Count(TotalNode, At(5), 0), Count(RejectNode, At(5), 0)}} );
		Replay( {Count(TotalNode, At(6,0,10), 50), Count(RejectNode, At(6,0,20), 4)} );
		Machine->Finish( At(6,1) );
		EXPECT_EQ( Row(0), (Interval{At(6), 1min, 1min, {}, {}, 50, 46, 4}) );

		Sink = ms<Recorder>();
		Open( {Signals({{ERole::State, StateNode}, {ERole::Good, GoodNode}, {ERole::Reject, RejectNode}})}, {At(6), {Flag(StateNode, At(5), true), Count(GoodNode, At(5), 0), Count(RejectNode, At(5), 0)}} );
		Replay( {Count(GoodNode, At(6,0,10), 46), Count(RejectNode, At(6,0,20), 4)} );
		Machine->Finish( At(6,1) );
		EXPECT_EQ( Row(0), (Interval{At(6), 1min, 1min, {}, {}, 50, 46, 4}) );
	}

	//A record in a written interval is not applied:  the rows over it are to be rebuilt from the history, to its signal's
	//next record, or to now when it is its signal's newest.
	TEST_F( Streams, LateRecordAsksForARebuild ){
		auto& engine = Counting();
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,30), 110) );
		Time->AdvanceTo( At(6,1,4) );
		EXPECT_TRUE( Sink->Rows.empty() ) << "settle after its end";
		Time->AdvanceTo( At(6,1,6) );
		ASSERT_EQ( Sink->Rows.size(), 1u );
		EXPECT_EQ( Row(0), Minute(0, 1min, {}, {}, 10) );
		EXPECT_TP( engine.Processed(), At(6,1) );

		engine.Records( Stream({Count(TotalNode, At(6,0,20), 105)}) );
		ASSERT_EQ( Sink->Rebuilds.size(), 1u );
		EXPECT_EQ( Sink->Rebuilds[0], (Rebuilt{TotalNode, {At(6,0,20), At(6,0,30)}}) );
		engine.Records( Stream({Flag(StateNode, At(6,0,50), false)}) );
		ASSERT_EQ( Sink->Rebuilds.size(), 2u );
		EXPECT_EQ( Sink->Rebuilds[1], (Rebuilt{StateNode, {At(6,0,50), At(6,1,6)}}) );

		EXPECT_TRUE( Sink->Stops.empty() );
		EXPECT_EQ( engine.State().State, EState::Running );
		Time->AdvanceTo( At(6,2,5) );
		EXPECT_EQ( Row(1), Minute(1, 1min) );
	}

	//A Modification is placed by the time it targets:  in the open interval the tallies are made again, in a written one it is a rebuild.
	TEST_F( Streams, ModificationByTheTimeItTargets ){
		auto& engine = Counting();
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,30), 110) );
		Arrive( Flag(StateNode, At(6,0,40), false) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:40-"} );
		Time->AdvanceTo( At(6,0,50) );
		engine.Records( Edit(TotalNode, At(6,0,30), Count(TotalNode, At(6,0,30), 120)) );
		engine.Records( Edit(StateNode, At(6,0,40), nullopt, Hist::Proto::UPDATE_TYPE_DELETE) );
		EXPECT_TRUE( Sink->Stops.empty() );
		EXPECT_EQ( Sink->Calls.back(), "rewritten 2026-03-09T06:00:40Z" );
		engine.Records( Edit(StateNode, At(6,0,45), Flag(StateNode, At(6,0,45), false), Hist::Proto::UPDATE_TYPE_INSERT) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:45-"} );
		Arrive( Flag(StateNode, At(6,0,55), true) );
		Time->AdvanceTo( At(6,1,6) );
		EXPECT_EQ( Row(0), Minute(0, 50s, 10s, {}, 20) );
		EXPECT_TRUE( Sink->Rebuilds.empty() );

		engine.Records( Edit(TotalNode, At(6,0,30), Count(TotalNode, At(6,0,30), 125)) );
		ASSERT_EQ( Sink->Rebuilds.size(), 1u );
		EXPECT_EQ( Sink->Rebuilds[0], (Rebuilt{TotalNode, {At(6,0,30), At(6,1,6)}}) );
		engine.Records( Edit(StateNode, At(6,0,10), Flag(StateNode, At(6,0,10), false), Hist::Proto::UPDATE_TYPE_INSERT) );
		ASSERT_EQ( Sink->Rebuilds.size(), 2u );
		EXPECT_EQ( Sink->Rebuilds[1], (Rebuilt{StateNode, {At(6,0,10), At(6,0,55)}}) ) << "no further than the State's newest record the engine let go";
	}

	//Records come in any order:  one behind the cursor, in an interval not yet written, is placed by its source time.
	TEST_F( Streams, OutOfOrderInsideTheOpenInterval ){
		auto& engine = Counting();
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,50), 110) );
		engine.Records( Stream({Flag(StateNode, At(6,0,45), true), Flag(StateNode, At(6,0,40), false)}) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:40-06:00:45"} );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
		Time->AdvanceTo( At(6,1,5) );
		EXPECT_EQ( Row(0), Minute(0, 55s, 5s, {}, 10) );
	}

	//The stream that arrives while the snapshot is paged is held to its end.
	TEST_F( Streams, StreamHeldUntilLive ){
		auto& engine = Counting();
		engine.Records( Stream({Count(TotalNode, At(6,0,40), 120)}) );
		Replay( {Count(TotalNode, At(6,0,30), 110)} );
		EXPECT_TP( engine.State().At, At(6,0,30) );
		Time->AdvanceTo( At(6,0,41) );
		engine.Live();
		Time->AdvanceTo( At(6,1,5) );
		EXPECT_EQ( Row(0), Minute(0, 1min, {}, {}, 20) );
	}

	//The backfill's end writes every interval the clock has passed, a quiet night's among them.
	TEST_F( Streams, SnapshotEndClosesWhatTheClockPassed ){
		auto& engine = Counting();
		Replay( {Flag(StateNode, At(6,0,30), false)} );
		Time->AdvanceTo( At(9,0,10) );
		engine.Live();
		ASSERT_EQ( Sink->Rows.size(), 180u );
		EXPECT_EQ( Row(0), Minute(0, 30s, 30s) );
		EXPECT_EQ( Sink->Rows.back(), (Interval{At(8,59), 1min, {}, 1min}) );
		EXPECT_TP( engine.Processed(), At(9) );
	}

	//The clock, not a record, finds the stop:  it began at the last count, so the rows written since are to be rebuilt.
	TEST_F( Streams, IdleDetectedByTheClock ){
		auto& engine = CountOnly( 3min );
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,10), 101) );
		Time->AdvanceTo( At(6,3,9) );
		ASSERT_EQ( Sink->Rows.size(), 3u );
		EXPECT_EQ( Row(2), Minute(2, 1min) ) << "running while counts arrive";
		EXPECT_TRUE( Sink->Stops.empty() );
		Time->AdvanceTo( At(6,3,10) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:10-"} );
		ASSERT_EQ( Sink->Rebuilds.size(), 1u );
		EXPECT_EQ( Sink->Rebuilds[0], (Rebuilt{TotalNode, {At(6,0,10), At(6,3,10)}}) );
		EXPECT_EQ( Sink->States.back().State, EState::Stopped );
		EXPECT_TP( Sink->States.back().Since, At(6,0,10) );

		Time->AdvanceTo( At(6,4,5) );
		EXPECT_EQ( Row(3), Minute(3, {}, 1min) );
		Arrive( Count(TotalNode, At(6,5,20), 102) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:10-06:05:20"} );
		EXPECT_EQ( Sink->Calls.back(), "closed 06:00:10-06:05:20" );
		Time->AdvanceTo( At(6,6,5) );
		EXPECT_EQ( Row(5), Minute(5, 40s, 20s, {}, 1) );
		EXPECT_EQ( Sink->Rebuilds.size(), 1u );
	}
	//What the rebuild reads back:  a replay finds the same stop from the records' times, and asks for nothing.
	TEST_F( Streams, ReplayReproducesTheIdleStop ){
		auto& engine = CountOnly( 3min );
		Replay( {Count(TotalNode, At(6,0,10), 101), Count(TotalNode, At(6,5,20), 102)} );
		engine.Finish( At(6,6) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:10-06:05:20"} );
		EXPECT_EQ( Row(0), Minute(0, 10s, 50s, {}, 1) );
		EXPECT_EQ( Row(3), Minute(3, {}, 1min) );
		EXPECT_EQ( Row(5), Minute(5, 40s, 20s, {}, 1) );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
	}
	//An idle stop that began in an interval not yet written takes back what was tallied as run, and no rebuild.
	TEST_F( Streams, IdleInsideTheOpenInterval ){
		auto& engine = CountOnly( 52s, {{ERole::Reason, ReasonNode}} );
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,10), 101) );
		Arrive( Code(ReasonNode, At(6,0,25), 0) );//the other signal's records move the tallies on, as run.
		Arrive( Code(ReasonNode, At(6,1,1), 0) );
		EXPECT_TRUE( Sink->Stops.empty() );
		Time->AdvanceTo( At(6,1,2) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:10-"} );
		engine.Records( Stream({Code(ReasonNode, At(6,1,0), 0)}) );//behind the cursor:  tallied again from the minute, which the stop reached.
		EXPECT_EQ( Sink->Table(), Table{"06:00:10-"} );
		Time->AdvanceTo( At(6,2,5) );
		EXPECT_EQ( Row(0), Minute(0, 10s, 50s, {}, 1) );
		EXPECT_EQ( Row(1), Minute(1, {}, 1min) );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
	}

	//A Bad record on the counter of a machine with no State signal is unknown time from the record, never a stop.
	TEST_F( Streams, BadCounterIsUnknownNotAStop ){
		auto& engine = CountOnly( 2min );
		Replay( {Count(TotalNode, At(6,0,40), 102), Bad(TotalNode, At(6,1,10), UA_STATUSCODE_BADNOCOMMUNICATION), Count(TotalNode, At(6,4), 103)} );
		engine.Finish( At(6,5) );
		EXPECT_EQ( Sink->Table(), Table{"06:01:10-06:04:00?"} );
		EXPECT_EQ( Row(1), Minute(1, 10s, {}, 50s) );
		EXPECT_EQ( Row(3), Minute(3, {}, {}, 1min) );
		EXPECT_EQ( Row(4), Minute(4, 1min, {}, {}, 1) );
	}
	//After unknown time, a Good record that counts nothing reads stopped.
	TEST_F( Streams, NoCountAfterAGapReadsStopped ){
		auto& engine = CountOnly( 2min );
		Replay( {Bad(TotalNode, At(6,0,10)), Count(TotalNode, At(6,0,40), 100), Count(TotalNode, At(6,1), 101)} );
		engine.Finish( At(6,2) );
		EXPECT_EQ( Sink->Table(), (Table{"06:00:10-06:00:40?", "06:00:40-06:01:00"}) );
	}
	//The marker of an outage comes with the first value after it:  the idle stop the clock found meanwhile is replaced.
	TEST_F( Streams, MarkerTakesBackTheIdleStop ){
		auto& engine = CountOnly( 20s );
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,10), 101) );
		Time->AdvanceTo( At(6,0,30) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:10-"} );
		Time->AdvanceTo( At(6,0,50) );
		engine.Records( Stream({Bad(TotalNode, At(6,0,20)), Count(TotalNode, At(6,0,50), 104)}) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:20-06:00:50?"} );
		EXPECT_EQ( Sink->Calls.back(), "rewritten 2026-03-09T06:00:10Z 06:00:20-06:00:50?" );
		Time->AdvanceTo( At(6,1,5) );
		EXPECT_EQ( Row(0), Minute(0, 30s, {}, 30s, 4) );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
	}

	//Stepped:  the value times the time it held over the period, split at the minutes, nothing while it is Bad.
	TEST_F( Streams, RateIntegratedOverItsPeriod ){
		Open( {Signals({{ERole::State, StateNode}, {ERole::Rate, RateNode}})}, {At(6), {Flag(StateNode, At(5,50), true), Number(RateNode, At(5,50), 120)}} );
		Replay( {Number(RateNode, At(6,0,30), 60), Bad(RateNode, At(6,1,30), UA_STATUSCODE_BADNOCOMMUNICATION), Number(RateNode, At(6,2,30), 30)} );
		Machine->Finish( At(6,4) );
		EXPECT_DOUBLE_EQ( Row(0).Total, 90 );
		EXPECT_DOUBLE_EQ( Row(1).Total, 30 );
		EXPECT_DOUBLE_EQ( Row(2).Total, 15 );
		EXPECT_DOUBLE_EQ( Row(3).Total, 30 );
		EXPECT_EQ( Row(1).Run, 1min ) << "the State signal's, not the rate's";

		Sink = ms<Recorder>();
		auto perSecond = Signals( {{ERole::State, StateNode}, {ERole::Rate, RateNode}} );
		perSecond[1].RatePeriod = 1s;
		Open( {perSecond}, {At(6), {Flag(StateNode, At(5,50), true), Number(RateNode, At(5,50), 2)}} );
		Machine->Finish( At(6,1) );
		EXPECT_DOUBLE_EQ( Row(0).Total, 120 );
	}
	//With no State signal a rate above zero runs the machine, and the idle timeout runs from its fall.
	TEST_F( Streams, RateAsTheState ){
		EngineConfig config{ Signals({{ERole::Rate, RateNode}}) };
		config.IdleTimeout = 1min;
		Open( config, {At(6), {Number(RateNode, At(5,50), 10)}} );
		Replay( {Number(RateNode, At(6,0,30), 0), Number(RateNode, At(6,3), 6)} );
		Machine->Finish( At(6,4) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:30-06:03:00"} );
		EXPECT_DOUBLE_EQ( Row(0).Total, 5 );
		EXPECT_EQ( Row(0).Run, 30s );
		EXPECT_DOUBLE_EQ( Row(3).Total, 6 );
	}

	//The open interval closes at its old end, the minutes to the new width's boundary keep the old width, and the rows
	//after it are the new one's.
	TEST_F( Streams, WidthChangeAndTheRowsBetween ){
		auto& engine = Open( {Signals({{ERole::State, StateNode}})}, {At(10), {Flag(StateNode, At(9), true)}} );
		Replay( {Flag(StateNode, At(10,7,30), false)} );
		engine.SetWidth( 15min );
		Replay( {Flag(StateNode, At(10,20), true)} );
		engine.Finish( At(10,45) );
		ASSERT_EQ( Sink->Rows.size(), 17u );
		for( uint i=0; i<15; ++i ){
			EXPECT_TP( Sink->Rows[i].Start, At(10, (int)i) );
			EXPECT_EQ( Sink->Rows[i].Width, 1min );
		}
		EXPECT_EQ( Sink->Rows[7], (Interval{At(10,7), 1min, 30s, 30s}) );
		EXPECT_EQ( Sink->Rows[15], (Interval{At(10,15), 15min, 10min, 5min}) );
		EXPECT_EQ( Sink->Rows[16], (Interval{At(10,30), 15min, 15min}) );
	}
	//Twenty minutes to fifteen:  the old width runs to the hour, the first end both share.
	TEST_F( Streams, WidthChangeWaitsForASharedBoundary ){
		EngineConfig config{ Signals({{ERole::State, StateNode}}) };
		config.Width = 20min;
		auto& engine = Open( config, {At(10), {Flag(StateNode, At(9), true)}} );
		Replay( {Flag(StateNode, At(10,7), false)} );
		engine.SetWidth( 15min );
		engine.Finish( At(11,30) );
		ASSERT_EQ( Sink->Rows.size(), 5u );
		EXPECT_EQ( Sink->Rows[0], (Interval{At(10), 20min, 7min, 13min}) );
		EXPECT_EQ( Sink->Rows[2], (Interval{At(10,40), 20min, {}, 20min}) );
		EXPECT_EQ( Sink->Rows[3], (Interval{At(11), 15min, {}, 15min}) );
		EXPECT_EQ( Sink->Rows[4], (Interval{At(11,15), 15min, {}, 15min}) );
		EXPECT_FALSE( Refused([&]{ engine.SetWidth(7min); }).empty() );
	}

	//The Reason signal's value at the stop's start when it is not zero, else its first above zero before the stop's end.
	TEST_F( Streams, ReasonCode ){
		Open( {Signals({{ERole::State, StateNode}, {ERole::Reason, ReasonNode}})}, {At(6), {Flag(StateNode, At(5), true), Code(ReasonNode, At(5), 0)}} );
		Replay( {
			Code(ReasonNode, At(6,0,59), 7), Flag(StateNode, At(6,1), false), Flag(StateNode, At(6,2), true), Code(ReasonNode, At(6,2), 0),
			Flag(StateNode, At(6,3), false), Code(ReasonNode, At(6,3,5), 8), Code(ReasonNode, At(6,3,30), 9), Flag(StateNode, At(6,4), true), Code(ReasonNode, At(6,4), 0),
			Flag(StateNode, At(6,5), false), Flag(StateNode, At(6,6), true)} );
		Machine->Finish( At(6,7) );
		EXPECT_EQ( Sink->Table(), (Table{"06:01:00-06:02:00#7", "06:03:00-06:04:00#8", "06:05:00-06:06:00"}) );
	}
	//Live, a stop's row is written as it begins, its code once the signal gives one, and its end when it ends.
	TEST_F( Streams, StopOpenedCodedAndClosed ){
		Open( {Signals({{ERole::State, StateNode}, {ERole::Reason, ReasonNode}})}, {At(6), {Flag(StateNode, At(5), true), Code(ReasonNode, At(5), 0)}} );
		Machine->Live();
		Arrive( Flag(StateNode, At(6,0,10), false) );
		Arrive( Code(ReasonNode, At(6,0,12), 4) );
		Arrive( Flag(StateNode, At(6,0,40), true) );
		EXPECT_EQ( Sink->Calls, (Table{"opened 06:00:10-", "rewritten 2026-03-09T06:00:10Z 06:00:10-#4", "closed 06:00:10-06:00:40#4"}) );
	}

	//A State value that means running is one of the signal's list.
	TEST_F( Streams, RunningList ){
		auto signals = Signals( {{ERole::State, StateNode}} );
		signals[0].Running = { StateValue{2.0}, StateValue{3.0}, StateValue{string{"RUN"}} };
		Open( {signals}, {At(6), {Code(StateNode, At(5), 2)}} );
		auto text = Stamp( StateNode, At(6,3) );
		text.mutable_value()->set_string_value( "RUN" );
		Replay( {Code(StateNode, At(6,1), 1), Code(StateNode, At(6,1,30), 5), Code(StateNode, At(6,2), 3), Flag(StateNode, At(6,2,30), true), text} );
		Machine->Finish( At(6,4) );
		EXPECT_EQ( Sink->Table(), (Table{"06:01:00-06:02:00", "06:02:30-06:03:00"}) ) << "any other Good value is stopped, and one stop holds through two of them";
	}

	//The row a restart found open goes on where the bound agrees, and ends at the opening where it does not.
	TEST_F( Streams, RestartContinuesTheOpenStop ){
		Sink->Stops.emplace( At(5,40), Stop{At(5,40)} );
		Open( {Signals({{ERole::State, StateNode}})}, {At(6), {Flag(StateNode, At(5,40), false)}, Stop{At(5,40)}} );
		Machine->Replay( {} );
		EXPECT_TRUE( Sink->Calls.empty() );
		EXPECT_TP( Machine->State().Since, At(5,40) );
		Replay( {Flag(StateNode, At(6,1,30), true)} );
		EXPECT_EQ( Sink->Calls, Table{"closed 05:40:00-06:01:30"} );
	}
	TEST_F( Streams, BoundOutranksTheOpenStop ){
		Sink->Stops.emplace( At(5,40), Stop{At(5,40)} );
		Open( {Signals({{ERole::State, StateNode}})}, {At(6), {Bad(StateNode, At(5,55))}, Stop{At(5,40)}} );
		Machine->Finish( At(6,1) );
		EXPECT_EQ( Sink->Calls, (Table{"closed 05:40:00-06:00:00", "opened 06:00:00-?"}) );
		EXPECT_EQ( Row(0), Minute(0, {}, {}, 1min) );
	}
	//With no record before it a signal starts unknown, a counter's first value is what the next counts against, and the
	//first row runs from the opening to the width's next boundary.
	TEST_F( Streams, NoBoundIsUnknown ){
		Open( {Signals({{ERole::State, StateNode}, {ERole::Total, TotalNode}})}, {At(6,0,20), {NoBound(StateNode, At(6,0,20))}} );
		Replay( {Count(TotalNode, At(6,0,30), 500), Flag(StateNode, At(6,0,40), true), Count(TotalNode, At(6,0,50), 510)} );
		Machine->Finish( At(6,2) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:20-06:00:40?"} );
		ASSERT_EQ( Sink->Rows.size(), 2u );
		EXPECT_EQ( Sink->Rows[0], (Interval{At(6,0,20), 40s, 20s, {}, 20s, 10, 10, 0}) );
		EXPECT_EQ( Sink->Rows[1], Minute(1, 1min) );
	}
	//A count that was idle before the opening is stopped from it:  nothing before the opening is this run's to rewrite.
	TEST_F( Streams, IdleBeforeTheOpening ){
		EngineConfig config{ Signals({{ERole::Total, TotalNode}}) };
		config.IdleTimeout = 1min;
		Open( config, {At(6), {Count(TotalNode, At(5,30), 100)}} );
		Machine->Finish( At(6,1) );
		EXPECT_EQ( Sink->Table(), Table{"06:00:00-"} );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
	}

	//The state at once on a change, the stop in progress with its code, and Small until the stop reaches the threshold.
	TEST_F( Streams, LiveStateAndSmall ){
		auto& engine = Counting();
		engine.Live();
		ASSERT_EQ( Sink->States.size(), 1u );
		EXPECT_EQ( Sink->States[0].State, EState::Running );
		EXPECT_TP( Sink->States[0].Since, At(6) );
		Arrive( Flag(StateNode, At(6,1), false) );
		ASSERT_EQ( Sink->States.size(), 2u );
		EXPECT_EQ( Sink->States[1].State, EState::Stopped );
		EXPECT_TP( Sink->States[1].Open->Start, At(6,1) );
		EXPECT_TRUE( Sink->States[1].Small );
		Time->AdvanceTo( At(6,5,59) );
		EXPECT_EQ( Sink->States.size(), 2u );
		Time->AdvanceTo( At(6,6) );
		ASSERT_EQ( Sink->States.size(), 3u );
		EXPECT_FALSE( Sink->States[2].Small );
		Arrive( Count(TotalNode, At(6,6,30), 130) );
		EXPECT_EQ( Sink->States.size(), 3u ) << "a count alone is no state change";

		let now = engine.State();
		EXPECT_TP( now.Processed, At(6,6) );
		EXPECT_TP( now.At, At(6,6,30) );
		EXPECT_TP( *now.LastCount, At(6,6,30) );
		ASSERT_EQ( now.Rows.size(), 1u );
		EXPECT_EQ( now.Rows[0], (Interval{At(6,6), 30s, {}, 30s, {}, 30, 30, 0}) ) << "the open interval's tallies so far";
		Time->AdvanceTo( At(6,6,45) );
		EXPECT_EQ( engine.State().Rows[0].Stop, 45s ) << "to the clock, with no record";
		Arrive( Flag(StateNode, At(6,7), true) );
		ASSERT_EQ( Sink->States.size(), 4u );
		EXPECT_EQ( Sink->States[3].State, EState::Running );
		EXPECT_FALSE( Sink->States[3].Open );
	}

	//A server whose clock lags is not late by the lag:  the engine's clock is the wall clock less the source offset.
	TEST_F( Streams, SourceOffset ){
		auto& engine = Counting();
		engine.Live( 10min );//what the historian holds for the nodes.
		Arrive( Count(TotalNode, At(6,0,30), 110), 10min );
		Time->AdvanceTo( At(6,11,4) );
		EXPECT_TRUE( Sink->Rows.empty() );
		Time->AdvanceTo( At(6,11,5) );
		ASSERT_EQ( Sink->Rows.size(), 1u );
		Arrive( Count(TotalNode, At(6,1,10), 115), 10min );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
		Time->AdvanceTo( At(6,12,5) );
		EXPECT_EQ( Row(1), Minute(1, 1min, {}, {}, 5) );
	}
	//The stream keeps it:  the least arrival less source time of its last few Good values, so one that was held back moves nothing.
	TEST_F( Streams, SourceOffsetFromTheStream ){
		auto& engine = Counting();
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,30), 110), 3s );
		engine.Records( Stream({Flag(StateNode, At(6,0,10), true)}) );
		Time->AdvanceTo( At(6,1,7) );
		EXPECT_TRUE( Sink->Rows.empty() );
		Time->AdvanceTo( At(6,1,8) );
		EXPECT_EQ( Sink->Rows.size(), 1u );
	}

	//A source found to lag only once the stream says so:  its first record is doubted, the second that agrees believed.
	TEST_F( Streams, SourceLagLearned ){
		auto& engine = Counting();
		engine.Live();
		Time->AdvanceTo( At(6,10,30) );
		EXPECT_EQ( Sink->Rows.size(), 10u ) << "by the wall clock, until a record says otherwise";
		engine.Records( Stream({Count(TotalNode, At(6,0,30), 110)}) );
		EXPECT_EQ( engine.State().Offset, 0s );
		Time->AdvanceTo( At(6,10,40) );
		engine.Records( Stream({Count(TotalNode, At(6,0,40), 111)}) );
		EXPECT_EQ( engine.State().Offset, 10min );
		EXPECT_EQ( Sink->Rebuilds.size(), 2u ) << "both in written intervals";
		Time->AdvanceTo( At(6,12) );
		EXPECT_EQ( Sink->Rows.size(), 10u ) << "the engine's clock is the source's now";
	}
	//One record stamped decades ahead, a PLC's unset clock, moves neither the tallies nor the engine's clock.
	TEST_F( Streams, RecordFarAhead ){
		auto& engine = Counting();
		engine.Live();
		Arrive( Count(TotalNode, At(6,0,30), 110) );
		engine.Records( Stream({Count(TotalNode, sys_days{2100y/January/1}, 999)}) );
		EXPECT_TP( engine.State().At, At(6,0,30) );
		EXPECT_EQ( engine.State().Offset, 0s );
		Arrive( Count(TotalNode, At(6,0,50), 112) );
		Time->AdvanceTo( At(6,2,5) );
		ASSERT_EQ( Sink->Rows.size(), 2u );
		EXPECT_EQ( Row(0), Minute(0, 1min, {}, {}, 12) );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
	}
	//A backfill's too:  the stream is the clock only as far as the wall clock.
	TEST_F( Streams, ReplayedRecordFarAhead ){
		auto& engine = Counting();
		Time->AdvanceTo( At(6,3) );
		engine.Replay( Page({Count(TotalNode, At(6,0,30), 110), Count(TotalNode, sys_days{2100y/January/1}, 999)}) );
		engine.Live();
		EXPECT_EQ( Sink->Rows.size(), 2u );
		EXPECT_TP( engine.Processed(), At(6,2) );
	}

	TEST_F( Streams, EndedTakesNothing ){
		auto& engine = Counting();
		engine.Live();
		engine.End();
		Arrive( Flag(StateNode, At(6,0,30), false) );
		Time->AdvanceTo( At(6,5) );
		EXPECT_TRUE( Sink->Rows.empty() );
		EXPECT_TRUE( Sink->Stops.empty() );
	}

	TEST_F( Streams, RefusesSignalsTheRulesDo ){
		EXPECT_FALSE( Refused([&]{ Open( {Signals({{ERole::Good, GoodNode}})}, {At(6)} ); }).empty() );
		EngineConfig config{ Signals({{ERole::State, StateNode}}) };
		config.Width = 7min;
		EXPECT_FALSE( Refused([&]{ Open( config, {At(6)} ); }).empty() );
	}
}
//The formulas (docs/oee/spec.md *Figures*, *Intervals*, *The Gantt chart*):  the shift of the spec's Gantt chart, from its
//record stream through the engine to the figures, and rows written out by hand for the rest.
#include "helpers.h"

namespace Jde::Opc::Oee::Tests{
	using Calendar::EBucket;
	using Calendar::Schedule;

	//A shift from 06:00 for eight hours every day, in UTC, with its breaks in minutes from its start.
	Ξ DayShift( vector<Calendar::Break> breaks={{240, 20}}, uint16 dayStart=360 )ε->Schedule{
		Calendar::Pattern pattern{ 1, 1 };
		pattern.Shifts.push_back( {1, 1, "Day", 127, 0, 480, move(breaks)} );
		return Schedule{ 1, "Plant", "plant", *locate_zone("UTC"), dayStart, {pattern} };
	}
	const vector<Reason> Reasons{ {1, "Changeover", ECategory::Planned, 1}, {2, "Jam", ECategory::Unplanned, 2}, {3, "No orders", ECategory::Excluded} };
	constexpr Duration Cycle{ 1s };
	const Machine Pump{ 1, "Pump1", Cycle, 5min, true, true };

	//Rows of one width from `start`, running throughout, with each one's total.
	Ξ RunRows( TimePoint start, Duration width, std::initializer_list<double> totals )ι->vector<Interval>{
		vector<Interval> y;
		for( let total : totals ){
			y.push_back( {start, width, width, {}, {}, total, total, 0} );
			start += width;
		}
		return y;
	}
	Ξ Percent( optional<double> factor )ι->double{ return factor ? std::round( *factor*10000 )/100 : -1; }
	Ξ Tenth( optional<double> factor )ι->double{ return factor ? std::round( *factor*1000 )/10 : -1; }

	//The spec's shift, as its records:  a changeover the signal codes before the stop, a jam it codes just after, two small
	//stops, the feed down for ten minutes, an uncoded stop, and the last hour stopped, which an operator codes No orders.
	//16,848 units within planned time and 120 through the break, 842 rejected.
	struct GanttShift : EngineTest{
		α SetUp()->void override{
			Open( {Signals({{ERole::State, StateNode}, {ERole::Total, TotalNode}, {ERole::Reject, RejectNode}, {ERole::Reason, ReasonNode}}), 5min},
				{At(6), {Flag(StateNode, At(5), true), Count(TotalNode, At(5), 0), Count(RejectNode, At(5), 0), Code(ReasonNode, At(5), 0)}} );
			Replay( {
				Count(TotalNode, At(6,59,30), 2000),
				Code(ReasonNode, At(6,59,59), 1), Flag(StateNode, At(7), false), Flag(StateNode, At(7,30), true), Code(ReasonNode, At(7,30), 0),
				Flag(StateNode, At(8,12), false), Code(ReasonNode, At(8,12,5), 2), Flag(StateNode, At(8,40), true), Code(ReasonNode, At(8,40), 0),
				Flag(StateNode, At(9), false), Flag(StateNode, At(9,2), true),
				Count(TotalNode, At(9,59,30), 9000),
				Count(TotalNode, At(10,10,30), 9120),
				Bad(StateNode, At(11)), Flag(StateNode, At(11,10), true),
				Flag(StateNode, At(11,40), false), Flag(StateNode, At(11,42), true),
				Count(TotalNode, At(12,29,30), 15000),
				Flag(StateNode, At(12,30), false), Flag(StateNode, At(12,50), true),
				Count(TotalNode, At(12,59,30), 16968), Count(RejectNode, At(12,59,40), 842),
				Flag(StateNode, At(13), false)} );
			Machine->Finish( At(14) );
			uint32 id{};
			for( auto [_, stop] : Sink->Stops ){
				stop.Id = ++id;
				Stops.push_back( stop );
			}
		}
		α Rows()Ι->History{ return { Sink->Rows, Stops, Codings, Reasons }; }
		vector<Stop> Stops;
		vector<Coding> Codings{ {1, At(13), At(14), 3, "No orders until Tuesday", "ana"} };
		Schedule Calendar{ DayShift() };
	};

	TEST_F( GanttShift, Engine ){
		EXPECT_EQ( Sink->Rows.size(), 480u );
		EXPECT_EQ( Sink->Table(), (vector<string>{"07:00:00-07:30:00#1", "08:12:00-08:40:00#2", "09:00:00-09:02:00", "11:00:00-11:10:00?", "11:40:00-11:42:00", "12:30:00-12:50:00", "13:00:00-"}) );
		EXPECT_EQ( Sink->Row(At(12,59)), (Interval{At(12,59), 1min, 1min, {}, {}, 1968, 1126, 842}) );
		EXPECT_TRUE( Sink->Rebuilds.empty() );
	}
	TEST_F( GanttShift, Figures ){
		let buckets = Figures( Pump, Rows(), &Calendar, At(6), At(14), EBucket::Shift );
		ASSERT_EQ( buckets.size(), 1u );
		let& b = buckets[0];
		EXPECT_EQ( b.Shift, "Day" );
		EXPECT_TP( b.Start, At(6) );
		EXPECT_TP( b.End, At(14) );
		EXPECT_EQ( b.Planned, 390min ) << "its 480 less the break, the Excluded hour and the ten unknown minutes";
		EXPECT_EQ( b.Excluded, 60min );
		EXPECT_EQ( b.Unknown, 10min );
		EXPECT_EQ( b.Unplanned, 48min );
		EXPECT_EQ( b.PlannedStop, 30min );
		EXPECT_EQ( b.Small, 4min );
		EXPECT_EQ( b.Run, 312min );
		EXPECT_DOUBLE_EQ( b.Total, 16848 );
		EXPECT_DOUBLE_EQ( b.Reject, 842 );
		EXPECT_DOUBLE_EQ( b.Good, 16006 );
		EXPECT_DOUBLE_EQ( b.Outside, 120 ) << "a count in a break is in no factor";
		EXPECT_EQ( b.Ideal, 16848s );
		EXPECT_DOUBLE_EQ( Tenth(b.Availability), 80.0 );
		EXPECT_DOUBLE_EQ( Tenth(b.Performance), 90.0 );
		EXPECT_DOUBLE_EQ( Tenth(b.Quality), 95.0 );
		EXPECT_DOUBLE_EQ( Tenth(b.OEE), 68.4 );
		EXPECT_DOUBLE_EQ( Tenth(b.TEEP), 55.6 );
		EXPECT_EQ( b.Measured, EFactors::Availability|EFactors::Performance|EFactors::Quality );
		EXPECT_TRUE( b.Partial ) << "unknown time falls in it";
		EXPECT_EQ( b.Whole, 480min );
	}
	TEST_F( GanttShift, Losses ){
		let losses = Losses( Pump, Rows(), &Calendar, At(6), At(14) );
		ASSERT_EQ( losses.size(), 5u );
		auto row = [&]( uint i ){ let& l = losses[i]; return Ƒ( "{} {} {}", l.Cause ? l.Cause->Name : l.Kind==ELoss::Small ? "small" : "uncoded", Min(l.Time), l.Stops ); };
		EXPECT_EQ( row(0), "No orders 60 1" );
		EXPECT_EQ( row(1), "Changeover 30 1" );
		EXPECT_EQ( row(2), "Jam 28 1" );
		EXPECT_EQ( row(3), "uncoded 20 1" );
		EXPECT_EQ( row(4), "small 4 2" );
	}
	TEST_F( GanttShift, Timeline ){
		let timeline = Timeline( Pump, Rows(), &Calendar, At(6), At(14) );
		ASSERT_EQ( timeline.Planned.size(), 1u );
		EXPECT_EQ( timeline.Planned[0].Name, "Day" );
		ASSERT_EQ( timeline.Planned[0].Breaks.size(), 1u );
		EXPECT_TP( timeline.Planned[0].Breaks[0].Start, At(10) );
		vector<string> segments;
		for( let& s : timeline.Segments ){
			segments.push_back( std::format("{:%R}-{:%R} {}", floor<minutes>(s.Start), floor<minutes>(s.End), ToString(s.State)) );
			if( s.Cause )
				segments.back() += " "+s.Cause->Name;
			if( s.Uncoded )
				segments.back() += " uncoded";
			if( s.Small )
				segments.back() += " small";
		}
		EXPECT_EQ( segments, (vector<string>{
			"06:00-07:00 running", "07:00-07:30 stopped Changeover", "07:30-08:12 running", "08:12-08:40 stopped Jam", "08:40-09:00 running",
			"09:00-09:02 stopped uncoded small", "09:02-11:00 running", "11:00-11:10 unknown", "11:10-11:40 running", "11:40-11:42 stopped uncoded small",
			"11:42-12:30 running", "12:30-12:50 stopped uncoded", "12:50-13:00 running", "13:00-14:00 stopped No orders"}) );
		EXPECT_EQ( timeline.Segments.back().CodingId, 1u );
		EXPECT_EQ( timeline.Segments[1].StopId, 1u );
		EXPECT_EQ( timeline.Segments[7].StopId, 4u ) << "the unknown span's row";
	}
	//A coding over part of a stop splits its time, and one overrides the signal's code:  no recompute, the next read has it.
	TEST_F( GanttShift, CodingSplitsAStop ){
		Codings.insert( Codings.begin(), {2, At(8,30), At(8,40), 1, "Changeover began before the jam cleared", "ana"} );
		let b = Figures( Pump, Rows(), &Calendar, At(6), At(14) )[0];
		EXPECT_EQ( b.Unplanned, 38min );
		EXPECT_EQ( b.PlannedStop, 40min );
		EXPECT_EQ( b.Run, 312min );
		let timeline = Timeline( Pump, Rows(), &Calendar, At(8), At(9) );
		ASSERT_EQ( timeline.Segments.size(), 4u );
		EXPECT_EQ( timeline.Segments[1].Cause->Name, "Jam" );
		EXPECT_TP( timeline.Segments[1].End, At(8,30) );
		EXPECT_EQ( timeline.Segments[2].Cause->Name, "Changeover" );
		EXPECT_EQ( timeline.Segments[2].StopId, timeline.Segments[1].StopId );
		let losses = Losses( Pump, Rows(), &Calendar, At(6), At(14) );
		EXPECT_EQ( losses[1].Cause->Name, "Changeover" );
		EXPECT_EQ( losses[1].Time, 40min );
		EXPECT_EQ( losses[1].Stops, 2u );
	}
	//The thresholds are read-time:  a changed small-stop threshold reclassifies the past at the next read.
	TEST_F( GanttShift, ThresholdReclassifies ){
		auto pump = Pump;
		pump.SmallStop = 1min;
		let b = Figures( pump, Rows(), &Calendar, At(6), At(14) )[0];
		EXPECT_EQ( b.Small, Duration::zero() );
		EXPECT_EQ( b.Unplanned, 52min );
		pump.SmallStop = 25min;
		let c = Figures( pump, Rows(), &Calendar, At(6), At(14) )[0];
		EXPECT_EQ( c.Small, 24min ) << "the uncoded twenty minutes too;  the jam is longer, and Excluded stays Excluded";
		EXPECT_EQ( c.Unplanned, 28min );
		EXPECT_EQ( c.Excluded, 60min );
	}
	//The figures are over what the rows cover:  read at 10:00, the shift's so far, flagged partial.
	TEST_F( GanttShift, SoFar ){
		auto history = Rows();
		history.Rows = history.Rows.first( 240 );
		let b = Figures( Pump, history, &Calendar, At(6), At(14), EBucket::Shift )[0];
		EXPECT_TP( b.End, At(14) );
		EXPECT_EQ( b.Whole, 240min );
		EXPECT_EQ( b.Planned, 240min );
		EXPECT_EQ( b.Run, 240min-30min-28min );
		EXPECT_TRUE( b.Partial );
	}

	//The textbook example:  a 420-minute planned shift with 47 minutes down, 19,271 pieces at a one-second cycle, 423 rejects.
	TEST( FiguresTests, Textbook ){
		let calendar = DayShift( {{120, 15}, {240, 30}, {360, 15}} );
		auto rows = RunRows( At(6), 1h, {4000, 4000, 0, 3271, 0, 4000, 0, 4000} );
		rows[3].Run = 13min; rows[3].Stop = 47min;
		rows[7].Reject = 423; rows[7].Good -= 423;
		const vector<Stop> stops{ {At(9,10), At(9,57)} };
		let b = Figures( Pump, {rows, stops, {}, Reasons}, &calendar, At(6), At(14) )[0];
		EXPECT_EQ( b.Planned, 420min );
		EXPECT_EQ( b.Unplanned, 47min );
		EXPECT_EQ( b.Run, 373min );
		EXPECT_DOUBLE_EQ( b.Total, 19271 );
		EXPECT_DOUBLE_EQ( b.Outside, 0 );
		EXPECT_DOUBLE_EQ( Percent(b.Availability), 88.81 );
		EXPECT_DOUBLE_EQ( Percent(b.Performance), 86.11 );
		EXPECT_DOUBLE_EQ( Percent(b.Quality), 97.80 );
		EXPECT_DOUBLE_EQ( Percent(b.OEE), 74.79 );
		EXPECT_FALSE( b.Partial );
	}

	//OEE is the product of the factors measured, and the result says which they were.
	TEST( FiguresTests, EachFactorUnmeasured ){
		let calendar = DayShift( {} );
		auto rows = RunRows( At(6), 1h, {900, 900, 900, 900, 900, 900, 900, 900} );
		for( auto& row : rows ){
			row.Good = 810;
			row.Reject = 90;
		}
		const vector<Stop> stops{ {At(9), At(11)} };
		const History history{ rows, stops, {}, Reasons };
		auto figures = [&]( Machine machine ){ return Figures( machine, history, &calendar, At(6), At(14) )[0]; };

		let all = figures( {1, "all", 20s, 5min, true, true} );
		EXPECT_DOUBLE_EQ( *all.Availability, 0.75 );
		EXPECT_DOUBLE_EQ( *all.Performance, 7200*20.0/(360*60) );
		EXPECT_DOUBLE_EQ( *all.Quality, 0.9 );
		EXPECT_DOUBLE_EQ( *all.OEE, 0.75*(7200*20.0/(360*60))*0.9 );
		EXPECT_DOUBLE_EQ( *all.TEEP, *all.OEE );

		let noCycle = figures( {1, "no cycle", nullopt, 5min, true, true} );
		EXPECT_FALSE( noCycle.Performance );
		EXPECT_DOUBLE_EQ( *noCycle.OEE, 0.75*0.9 );
		EXPECT_EQ( noCycle.Measured, EFactors::Availability|EFactors::Quality );
		EXPECT_EQ( noCycle.Ideal, Duration::zero() );

		let noQuality = figures( {1, "total alone", 20s, 5min, true, false} );
		EXPECT_FALSE( noQuality.Quality );
		EXPECT_DOUBLE_EQ( *noQuality.OEE, 0.75*(7200*20.0/(360*60)) );
		EXPECT_EQ( noQuality.Measured, EFactors::Availability|EFactors::Performance );

		let stateAlone = figures( {1, "state alone", 20s, 5min, false, false} );
		EXPECT_FALSE( stateAlone.Performance );
		EXPECT_FALSE( stateAlone.Quality );
		EXPECT_DOUBLE_EQ( *stateAlone.OEE, 0.75 );
		EXPECT_EQ( stateAlone.Measured, EFactors::Availability );

		let fast = figures( {1, "wrong cycle", 40s, 5min, true, true} );
		EXPECT_GT( *fast.Performance, 1 ) << "above one as it is:  it says the cycle is wrong";

		//No planned time, a night:  nothing measured, and what it made is outside.
		auto late = RunRows( At(14), 1h, {50, 50} );
		let off = Figures( {1, "all", 20s, 5min, true, true}, {late, {}, {}, Reasons}, &calendar, At(14), At(16) )[0];
		EXPECT_EQ( off.Planned, Duration::zero() );
		EXPECT_FALSE( off.Availability );
		EXPECT_FALSE( off.OEE );
		EXPECT_FALSE( off.TEEP );
		EXPECT_EQ( off.Measured, EFactors::None );
		EXPECT_DOUBLE_EQ( off.Outside, 100 );
		EXPECT_DOUBLE_EQ( off.Total, 0 );
	}

	//A shift that starts at 06:10 under fifteen-minute rows:  the row across it is split by time, its counts and its
	//unknown time taken as even across it.
	TEST( FiguresTests, ShiftBoundaryInsideARow ){
		let calendar = DayShift( {}, 370 );
		auto rows = RunRows( At(6), 15min, {150, 150, 150, 150} );
		rows[0].Run = 12min; rows[0].Unknown = 3min;
		const History history{ rows, {}, {}, Reasons };
		let shift = Figures( Pump, history, &calendar, At(6), At(7), EBucket::Shift );
		ASSERT_EQ( shift.size(), 1u );
		EXPECT_TP( shift[0].Start, At(6,10) );
		EXPECT_EQ( shift[0].Unknown, 1min );
		EXPECT_EQ( shift[0].Planned, 49min );
		EXPECT_DOUBLE_EQ( shift[0].Total, 500 );
		EXPECT_DOUBLE_EQ( shift[0].Outside, 0 );

		let hour = Figures( Pump, history, &calendar, At(6), At(7) )[0];
		EXPECT_EQ( hour.Planned, 49min );
		EXPECT_DOUBLE_EQ( hour.Total, 500 );
		EXPECT_DOUBLE_EQ( hour.Outside, 100 ) << "the ten minutes before the shift";
		EXPECT_EQ( hour.Whole, 60min );

		//By a duration, a row across a bucket's edge the same way.
		let twenty = Figures( Pump, history, &calendar, At(6), At(7), Calendar::BucketBy{Duration{20min}} );
		ASSERT_EQ( twenty.size(), 3u );
		EXPECT_DOUBLE_EQ( twenty[0].Total, 50+50 );
		EXPECT_DOUBLE_EQ( twenty[0].Outside, 100 );
		EXPECT_DOUBLE_EQ( twenty[1].Total, 100+100 );
		EXPECT_DOUBLE_EQ( twenty[2].Total, 50+150 );
	}

	//Rows of mixed widths, a width change between them, sum as before.
	TEST( FiguresTests, MixedWidths ){
		auto rows = RunRows( At(6), 1min, {10, 10, 10, 10, 10} );
		let wide = RunRows( At(6,5), 5min, {50, 50} );
		rows.insert( rows.end(), wide.begin(), wide.end() );
		let b = Figures( Pump, {rows, {}, {}, Reasons}, nullptr, At(6), At(6,15) )[0];
		EXPECT_DOUBLE_EQ( b.Total, 150 );
		EXPECT_EQ( b.Planned, 15min );
		EXPECT_FALSE( b.Partial );
	}

	//A reject recorded in a later row than its count:  the rows keep the sign, so a range over both nets out, and only a
	//bucket cut between them is held at zero.
	TEST( FiguresTests, RejectInALaterRow ){
		auto rows = RunRows( At(6), 1min, {50, 0} );
		rows[1].Reject = 4;
		rows[1].Good = -4;
		let read = [&rows]( TimePoint from, TimePoint to ){ return Figures( Pump, {rows, {}, {}, Reasons}, nullptr, from, to )[0]; };
		let both = read( At(6), At(6,2) );
		EXPECT_DOUBLE_EQ( both.Good, 46 );
		EXPECT_DOUBLE_EQ( both.Reject, 4 );
		EXPECT_DOUBLE_EQ( *both.Quality, 0.92 );
		let later = read( At(6,1), At(6,2) );
		EXPECT_DOUBLE_EQ( later.Good, 0 );
		EXPECT_DOUBLE_EQ( later.Reject, 0 );
	}
	//With Total and Good signals it is the good that can run ahead of its total.
	TEST( FiguresTests, GoodHeldToTheTotal ){
		auto rows = RunRows( At(6), 1min, {50} );
		rows[0].Good = 54;
		rows[0].Reject = -4;
		let b = Figures( Pump, {rows, {}, {}, Reasons}, nullptr, At(6), At(6,1) )[0];
		EXPECT_DOUBLE_EQ( b.Good, 50 );
		EXPECT_DOUBLE_EQ( b.Reject, 0 );
		EXPECT_DOUBLE_EQ( *b.Quality, 1 );
	}

	//With no calendar every minute is planned, the days begin at midnight UTC, and by shift is refused.
	TEST( FiguresTests, CalendarLess ){
		auto rows = RunRows( At(0)-24h, 1h, {} );
		for( auto t=At(0)-2h; t<At(26); t+=1h )
			rows.push_back( {t, 1h, 1h, {}, {}, 60, 60, 0} );
		const vector<Stop> stops{ {At(23), At(25)} };
		const History history{ rows, stops, {}, Reasons };
		let days = Figures( Pump, history, nullptr, At(0)-2h, At(26), EBucket::Day );
		ASSERT_EQ( days.size(), 3u );
		EXPECT_EQ( days[0].Shift, "2026-03-08" );
		EXPECT_EQ( days[1].Shift, "2026-03-09" );
		EXPECT_TP( days[1].Start, At(0) );
		EXPECT_TP( days[1].End, At(24) );
		EXPECT_EQ( days[1].Planned, 24h );
		EXPECT_EQ( days[1].Unplanned, 1h );
		EXPECT_EQ( days[2].Unplanned, 1h ) << "a stop across midnight, by the part inside";
		EXPECT_DOUBLE_EQ( days[1].Total, 24*60 );
		EXPECT_DOUBLE_EQ( days[1].Outside, 0 );
		EXPECT_DOUBLE_EQ( *days[1].TEEP, *days[1].OEE );
		let refusal = Refused( [&]{ Figures( Pump, history, nullptr, At(0), At(24), EBucket::Shift ); } );
		EXPECT_NE( refusal.find("no calendar"), string::npos ) << refusal;

		let timeline = Timeline( Pump, history, nullptr, At(22), At(26) );
		ASSERT_EQ( timeline.Planned.size(), 1u );
		EXPECT_FALSE( timeline.Planned[0].Shift );
		EXPECT_TP( timeline.Planned[0].Start, At(22) );
		EXPECT_TP( timeline.Planned[0].End, At(26) );
		ASSERT_EQ( timeline.Segments.size(), 3u );
		EXPECT_EQ( timeline.Segments[1].State, EState::Stopped );
	}

	//Nothing read is nothing measured:  no rows, or a range the rows do not reach.
	TEST( FiguresTests, NoRows ){
		let calendar = DayShift();
		let b = Figures( Pump, {{}, {}, {}, Reasons}, &calendar, At(6), At(14) )[0];
		EXPECT_TRUE( b.Partial );
		EXPECT_EQ( b.Planned, Duration::zero() );
		EXPECT_FALSE( b.OEE );
		EXPECT_TRUE( Figures(Pump, {{}, {}, {}, Reasons}, &calendar, At(14), At(6)).empty() );
		EXPECT_TRUE( Timeline(Pump, {{}, {}, {}, Reasons}, &calendar, At(6), At(14)).Segments.empty() );
		EXPECT_TRUE( Losses(Pump, {{}, {}, {}, Reasons}, &calendar, At(6), At(14)).empty() );
	}

	//An open stop is small by its length so far, to where the rows end.
	TEST( FiguresTests, OpenStopSoFar ){
		let rows = RunRows( At(6), 1min, {0, 0, 0, 0, 0, 0, 0, 0} );
		const vector<Stop> stops{ {At(6,4)} };
		let early = Figures( Pump, {std::span{rows}.first(7), stops, {}, Reasons}, nullptr, At(6), At(7) )[0];
		EXPECT_EQ( early.Small, 3min );
		EXPECT_EQ( early.Unplanned, Duration::zero() );
		auto more = RunRows( At(6), 1min, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0} );
		let later = Figures( Pump, {more, stops, {}, Reasons}, nullptr, At(6), At(7) )[0];
		EXPECT_EQ( later.Small, Duration::zero() );
		EXPECT_EQ( later.Unplanned, 6min );
	}
}
#include "helpers.h"

//Occurrences, Planned, At and Next over the plant calendar:  the night shift's day, both clock changes, the exceptions,
//the breaks, the rotation and a second pattern.
namespace Jde::Calendar::Tests{
	Ω names( const vector<Occurrence>& occurrences )ι->vector<string>{
		vector<string> y;
		for( let& o : occurrences )
			y.push_back( o.Name );
		return y;
	}

	//Wednesday 2026-07-01:  Day 10:00Z-18:00Z with its break at 14:00Z, Evening to 02:00Z Thursday, Night to 10:00Z Thursday.
	TEST( ResolveTests, OrdinaryDay ){
		let plant = Plant();
		let day = plant.Occurrences( Utc(2026,7,1,10), Utc(2026,7,2,10) );
		ASSERT_EQ( names(day), (vector<string>{"Day", "Evening", "Night"}) ) << "Tuesday's Night ends as the range starts and is out";
		EXPECT_EQ( day[0].Shift, 1u );
		EXPECT_EQ( day[0].Day, Ymd(2026,7,1) );
		EXPECT_TP( day[0].Start, Utc(2026,7,1,10) );
		EXPECT_TP( day[0].End, Utc(2026,7,1,18) );
		ASSERT_EQ( day[0].Breaks.size(), 1u );
		EXPECT_TP( day[0].Breaks[0].Start, Utc(2026,7,1,14) );
		EXPECT_TP( day[0].Breaks[0].End, Utc(2026,7,1,14,30) );
		EXPECT_TP( day[1].Start, Utc(2026,7,1,18) );
		EXPECT_TP( day[1].End, Utc(2026,7,2,2) ) << "22:00 EDT";
		EXPECT_TP( day[2].Start, Utc(2026,7,2,2) );
		EXPECT_TP( day[2].End, Utc(2026,7,2,10) );
		EXPECT_EQ( day[2].Day, Ymd(2026,7,1) );

		let inside = plant.Occurrences( Utc(2026,7,1,12), Utc(2026,7,1,13) );
		ASSERT_EQ( inside.size(), 1u ) << "a range inside a shift";
		EXPECT_TP( inside[0].Start, Utc(2026,7,1,10) ) << "the occurrence comes back whole";
		EXPECT_TP( inside[0].End, Utc(2026,7,1,18) );
		EXPECT_TRUE( plant.Occurrences(Utc(2026,7,1,12), Utc(2026,7,1,12)).empty() ) << "an empty range";

		let json = ToJson( day[0] );
		EXPECT_EQ( json.at("shift").to_number<uint32>(), 1u );
		EXPECT_EQ( json.at("name").as_string(), "Day" );
		EXPECT_EQ( json.at("day").as_string(), "2026-07-01" );
		EXPECT_EQ( json.at("start").as_string(), "2026-07-01T10:00:00Z" );
		EXPECT_EQ( json.at("breaks").as_array().size(), 1u );
	}

	//Friday's Night runs into Saturday and is Friday's;  the weekend has nothing until Monday's Day.
	TEST( ResolveTests, NightShiftBelongsToItsDay ){
		let plant = Plant();
		let saturdayNight = plant.Occurrences( Utc(2026,7,4,3), Utc(2026,7,4,4) );
		ASSERT_EQ( saturdayNight.size(), 1u );
		EXPECT_EQ( saturdayNight[0].Name, "Night" );
		EXPECT_EQ( saturdayNight[0].Day, Ymd(2026,7,3) ) << "started Friday 22:00";
		EXPECT_TP( saturdayNight[0].Start, Utc(2026,7,4,2) );
		EXPECT_TP( saturdayNight[0].End, Utc(2026,7,4,10) );
		EXPECT_TRUE( plant.Occurrences(Utc(2026,7,4,10), Utc(2026,7,6,10)).empty() ) << "Saturday 06:00 to Monday 06:00";
		let week = plant.Planned( Utc(2026,6,29,10), Utc(2026,7,6,10) );
		EXPECT_EQ( week.Total, 5*(8h+8h+7h+30min) ) << "five days of three shifts less the Day break";
		EXPECT_EQ( week.Spans.size(), 6u ) << "Monday's Day to its break, then each day's afternoon through the night into the next Day's break, and Friday's to Saturday 06:00";
		EXPECT_TP( week.Spans[0].End, Utc(2026,6,29,14) );
		EXPECT_TP( week.Spans[1].Start, Utc(2026,6,29,14,30) );
		EXPECT_TP( week.Spans[1].End, Utc(2026,6,30,14) ) << "Evening, Night and Tuesday's Day are back to back";
		EXPECT_TP( week.Spans[5].End, Utc(2026,7,4,10) );
	}

	//The night the clocks change is an hour short or long, as the crew's was:  7 hours in March, 9 in November.
	TEST( ResolveTests, ClockChangeNights ){
		let continuous = Continuous();
		let spring = continuous.Occurrences( Utc(2026,3,8), Utc(2026,3,8,12) );
		ASSERT_EQ( spring.size(), 1u );
		EXPECT_EQ( spring[0].Day, Ymd(2026,3,7) );
		EXPECT_TP( spring[0].Start, Utc(2026,3,8,3) ) << "22:00 EST";
		EXPECT_TP( spring[0].End, Utc(2026,3,8,10) ) << "06:00 EDT";
		EXPECT_EQ( continuous.Planned(Utc(2026,3,8), Utc(2026,3,8,12)).Total, 7h );

		let fall = continuous.Occurrences( Utc(2026,11,1), Utc(2026,11,1,12) );
		ASSERT_EQ( fall.size(), 1u );
		EXPECT_EQ( fall[0].Day, Ymd(2026,10,31) );
		EXPECT_TP( fall[0].Start, Utc(2026,11,1,2) ) << "22:00 EDT";
		EXPECT_TP( fall[0].End, Utc(2026,11,1,11) ) << "06:00 EST";
		EXPECT_EQ( continuous.Planned(Utc(2026,11,1), Utc(2026,11,1,12)).Total, 9h );
		EXPECT_EQ( continuous.Planned(Utc(2026,11,1), Utc(2026,11,1,2,30)).Total, 30min ) << "clipped to the range";
	}

	//A break in the hour the clocks skip, 02:00-03:00 in March, collapses to nothing;  one in the hour they repeat, 01:00-02:00
	//in November, is its first pass.
	TEST( ResolveTests, BreakInTheChange ){
		let c = Schedule{ CalendarRow(4, "breaks", NewYork, 0), jarray{ PatternRow(1, 4) }, jarray{ ShiftRow(1, 1, "Night", Everyday, 1380, 360, "[{\"start\":150,\"minutes\":30},{\"start\":180,\"minutes\":30}]") }, jarray{} };//23:00-05:00 with breaks 01:30-02:00 and 02:00-02:30.
		let spring = c.Occurrences( Utc(2026,3,8,5), Utc(2026,3,8,6) );
		ASSERT_EQ( spring.size(), 1u );
		ASSERT_EQ( spring[0].Breaks.size(), 1u ) << "02:00 and 02:30 EST both resolve to 03:00 EDT, so the second is gone";
		EXPECT_TP( spring[0].Breaks[0].Start, Utc(2026,3,8,6,30) ) << "01:30 EST";
		EXPECT_TP( spring[0].Breaks[0].End, Utc(2026,3,8,7) ) << "02:00 EST, the instant of the change";
		EXPECT_EQ( spring[0].End-spring[0].Start, 5h );
		let fall = c.Occurrences( Utc(2026,11,1,5), Utc(2026,11,1,6) );
		ASSERT_EQ( fall.size(), 1u );
		ASSERT_EQ( fall[0].Breaks.size(), 2u );
		EXPECT_TP( fall[0].Breaks[0].Start, Utc(2026,11,1,5,30) ) << "01:30 EDT, the first pass";
		EXPECT_TP( fall[0].Breaks[0].End, Utc(2026,11,1,7) ) << "02:00 happens only as EST, so each end resolved on its own makes the break 90 minutes";
		EXPECT_TP( fall[0].Breaks[1].Start, Utc(2026,11,1,7) ) << "02:00 EST happens once";
		EXPECT_EQ( fall[0].End-fall[0].Start, 7h );
	}

	//2-2-3 over a fortnight from Monday 2026-06-01:  on Monday and Tuesday, off two, on Friday to Sunday, then off two, on
	//Wednesday and Thursday, off three - and the same fortnight before the anchor.
	TEST( ResolveTests, Rotation ){
		constexpr uint32 twoTwoThree{ 1|2|16|32|64|512|1024 };
		let rotation = Schedule{ CalendarRow(3, "rotation"), jarray{ PatternRow(1, 3, {}, 14, Utc(2026,6,1,10)) }, jarray{ ShiftRow(1, 1, "A", twoTwoThree, 0, 720) }, jarray{} };
		vector<year_month_day> on;
		for( let& o : rotation.Occurrences(Utc(2026,5,18,10), Utc(2026,6,15,10)) )
			on.push_back( o.Day );
		let expected = vector<year_month_day>{ Ymd(2026,5,18), Ymd(2026,5,19), Ymd(2026,5,22), Ymd(2026,5,23), Ymd(2026,5,24), Ymd(2026,5,27), Ymd(2026,5,28),
			Ymd(2026,6,1), Ymd(2026,6,2), Ymd(2026,6,5), Ymd(2026,6,6), Ymd(2026,6,7), Ymd(2026,6,10), Ymd(2026,6,11) };
		EXPECT_EQ( on, expected );
	}

	//A holiday closes Friday whole, Night included;  a half hour closes a slice of Wednesday's Day, splitting it around the break.
	TEST( ResolveTests, ClosedExceptions ){
		let plant = Plant( jarray{ ExceptionRow(1, 1, "Holiday", Utc(2026,7,3,10), Utc(2026,7,4,10)), ExceptionRow(2, 1, "Fire drill", Utc(2026,7,1,14,30), Utc(2026,7,1,15)) } );
		let week = plant.Occurrences( Utc(2026,6,29,10), Utc(2026,7,6,10) );
		EXPECT_EQ( week.size(), 13u ) << "four days of three, one split in two";
		for( let& o : week )
			EXPECT_NE( o.Day, Ymd(2026,7,3) ) << o.Name;
		EXPECT_EQ( plant.Planned(Utc(2026,6,29,10), Utc(2026,7,6,10)).Total, 4*(8h+8h+7h+30min)-30min );

		let wednesday = plant.Occurrences( Utc(2026,7,1,10), Utc(2026,7,1,18) );
		ASSERT_EQ( names(wednesday), (vector<string>{"Day", "Day"}) );
		EXPECT_EQ( wednesday[0].Shift, 1u );
		EXPECT_EQ( wednesday[1].Shift, 1u );
		EXPECT_EQ( wednesday[1].Day, Ymd(2026,7,1) );
		EXPECT_TP( wednesday[0].Start, Utc(2026,7,1,10) );
		EXPECT_TP( wednesday[0].End, Utc(2026,7,1,14,30) );
		ASSERT_EQ( wednesday[0].Breaks.size(), 1u ) << "the break stays with the piece it is in";
		EXPECT_TP( wednesday[1].Start, Utc(2026,7,1,15) );
		EXPECT_TP( wednesday[1].End, Utc(2026,7,1,18) );
		EXPECT_TRUE( wednesday[1].Breaks.empty() );
		EXPECT_EQ( plant.Planned(Utc(2026,7,1,10), Utc(2026,7,1,18)).Total, 7h );

		let cutBreak = Plant( jarray{ ExceptionRow(1, 1, "Drill", Utc(2026,7,1,14,15), Utc(2026,7,1,14,45)) } );
		let pieces = cutBreak.Occurrences( Utc(2026,7,1,10), Utc(2026,7,1,18) );
		ASSERT_EQ( pieces.size(), 2u );
		ASSERT_EQ( pieces[0].Breaks.size(), 1u );
		EXPECT_TP( pieces[0].Breaks[0].End, Utc(2026,7,1,14,15) ) << "the break is clipped with its piece";
		EXPECT_TRUE( pieces[1].Breaks.empty() );
		EXPECT_EQ( cutBreak.Planned(Utc(2026,7,1,10), Utc(2026,7,1,18)).Total, 4h+3h+15min );
	}

	//An open Saturday is an occurrence with no shift;  one overlapping Friday's Night adds only what the shift leaves, and a
	//closed exception inside it beats it.
	TEST( ResolveTests, OpenExceptions ){
		let overtime = Plant( jarray{ ExceptionRow(1, 1, "Overtime", Utc(2026,7,11,12), Utc(2026,7,11,16), true) } );
		let saturday = overtime.Occurrences( Utc(2026,7,11,10), Utc(2026,7,12,10) );
		ASSERT_EQ( saturday.size(), 1u );
		EXPECT_FALSE( saturday[0].Shift );
		EXPECT_EQ( saturday[0].Name, "Overtime" );
		EXPECT_EQ( saturday[0].Day, Ymd(2026,7,11) );
		EXPECT_TP( saturday[0].Start, Utc(2026,7,11,12) );
		EXPECT_TP( saturday[0].End, Utc(2026,7,11,16) );
		EXPECT_TRUE( saturday[0].Breaks.empty() );
		EXPECT_EQ( overtime.Planned(Utc(2026,7,11,10), Utc(2026,7,12,10)).Total, 4h );
		EXPECT_EQ( ToJson(saturday[0]).at("shift"), jvalue{} ) << "null shift";

		let early = Plant( jarray{ ExceptionRow(1, 1, "Early", Utc(2026,7,11,8), Utc(2026,7,11,12), true) } );
		let pieces = early.Occurrences( Utc(2026,7,11,10), Utc(2026,7,12,10) );
		ASSERT_EQ( pieces.size(), 1u ) << "Friday's Night covers 08:00Z-10:00Z";
		EXPECT_TP( pieces[0].Start, Utc(2026,7,11,10) );
		EXPECT_TP( pieces[0].End, Utc(2026,7,11,12) );
		let whole = early.Occurrences( Utc(2026,7,11,9), Utc(2026,7,12,10) );
		ASSERT_EQ( names(whole), (vector<string>{"Night", "Early"}) ) << "the shift and the open piece, by start";

		let beaten = Plant( jarray{ ExceptionRow(1, 1, "Early", Utc(2026,7,11,8), Utc(2026,7,11,12), true), ExceptionRow(2, 1, "Closed", Utc(2026,7,11,11), Utc(2026,7,11,11,30)) } );
		let split = beaten.Occurrences( Utc(2026,7,11,10), Utc(2026,7,12,10) );
		ASSERT_EQ( split.size(), 2u );
		EXPECT_TP( split[0].End, Utc(2026,7,11,11) );
		EXPECT_TP( split[1].Start, Utc(2026,7,11,11,30) );
		EXPECT_EQ( beaten.Planned(Utc(2026,7,11,10), Utc(2026,7,12,10)).Total, 1h+30min );
	}

	//Day2 at 07:00 every day from Monday 2026-07-06:  Friday is the old pattern's, Sunday the 5th nothing, Monday and the next Sunday the new.
	TEST( ResolveTests, SecondPattern ){
		let plant = Plant( {}, jarray{ PatternRow(2, 1, Utc(2026,7,6,10)) }, jarray{ ShiftRow(4, 2, "Day2", Everyday, 60, 480) } );
		let friday = plant.Occurrences( Utc(2026,7,3,10), Utc(2026,7,3,18) );
		ASSERT_EQ( friday.size(), 1u );
		EXPECT_EQ( friday[0].Shift, 1u );
		EXPECT_TP( friday[0].Start, Utc(2026,7,3,10) );
		EXPECT_TRUE( plant.Occurrences(Utc(2026,7,5,10), Utc(2026,7,6,10)).empty() ) << "Sunday under the weekday pattern";
		let monday = plant.Occurrences( Utc(2026,7,6,10), Utc(2026,7,7,10) );
		ASSERT_EQ( names(monday), (vector<string>{"Day2"}) );
		EXPECT_EQ( monday[0].Shift, 4u );
		EXPECT_TP( monday[0].Start, Utc(2026,7,6,11) );
		EXPECT_TP( monday[0].End, Utc(2026,7,6,19) );
		EXPECT_EQ( plant.Occurrences(Utc(2026,7,12,10), Utc(2026,7,13,10)).size(), 1u ) << "Sunday under the new";
		let around = plant.Occurrences( Utc(2026,7,3,10), Utc(2026,7,7,10) );
		EXPECT_EQ( names(around), (vector<string>{"Day", "Evening", "Night", "Day2"}) ) << "Friday's three, the weekend's none, Monday's one";
	}

	TEST( ResolveTests, AtAndNext ){
		let plant = Plant();
		auto at = plant.At( Utc(2026,7,1,15) );
		ASSERT_TRUE( at );
		EXPECT_EQ( at->Name, "Day" );
		at = plant.At( Utc(2026,7,1,14,10) );
		ASSERT_TRUE( at ) << "a break is still inside the occurrence";
		EXPECT_EQ( at->Name, "Day" );
		at = plant.At( Utc(2026,7,1,9) );
		ASSERT_TRUE( at );
		EXPECT_EQ( at->Name, "Night" );
		EXPECT_EQ( at->Day, Ymd(2026,6,30) );
		at = plant.At( Utc(2026,7,1,18) );
		ASSERT_TRUE( at ) << "a boundary belongs to the shift starting";
		EXPECT_EQ( at->Name, "Evening" );
		EXPECT_FALSE( plant.At(Utc(2026,7,4,16)) ) << "Saturday afternoon";

		EXPECT_TP( *plant.Next(Utc(2026,7,1,13)), Utc(2026,7,1,14) ) << "the break starts";
		EXPECT_TP( *plant.Next(Utc(2026,7,1,14)), Utc(2026,7,1,14,30) ) << "the break ends";
		EXPECT_TP( *plant.Next(Utc(2026,7,1,14,30)), Utc(2026,7,1,18) ) << "Day ends";
		EXPECT_TP( *plant.Next(Utc(2026,7,1,18)), Utc(2026,7,2,2) ) << "Evening ends, 22:00 EDT";
		EXPECT_TP( *plant.Next(Utc(2026,7,4,2)), Utc(2026,7,4,10) ) << "Friday's Night ends";
		EXPECT_TP( *plant.Next(Utc(2026,7,4,16)), Utc(2026,7,6,10) ) << "across the weekend to Monday's Day";

		let holiday = Plant( jarray{ ExceptionRow(1, 1, "Shutdown", Utc(2026,7,6,10), Utc(2026,8,3,10)) } );
		EXPECT_TP( *holiday.Next(Utc(2026,7,4,16)), Utc(2026,8,3,10) ) << "a month closed:  the first shift after it";
		let empty = Schedule{ CalendarRow(5, "empty"), jarray{ PatternRow(1, 5) }, jarray{}, jarray{} };
		EXPECT_FALSE( empty.Next(Utc(2026,7,4,16)) ) << "nothing planned, ever";
		EXPECT_FALSE( empty.At(Utc(2026,7,4,16)) );
	}

	//Exception bounds keep whole seconds, so what the json says is what was resolved.
	TEST( ResolveTests, ExceptionBoundsWholeSeconds ){
		auto row = ExceptionRow( 1, 1, "Cut", Utc(2026,7,1,12), Utc(2026,7,1,13) );
		row["start"] = "2026-07-01T12:00:00.750Z";
		let plant = Plant( jarray{ row } );
		let day = plant.Occurrences( Utc(2026,7,1,10), Utc(2026,7,1,18) );
		ASSERT_EQ( day.size(), 2u );
		EXPECT_TP( day[0].End, Utc(2026,7,1,12) );
		EXPECT_EQ( ToJson(day[0]).at("end"), jvalue{"2026-07-01T12:00:00Z"} );
	}

	//Latest start, longest run:  a shift from the day before still counts, and one from two days before never does.
	TEST( ResolveTests, LongestShiftWalk ){
		let c = Schedule{ CalendarRow(3, "long", NewYork, 0), jarray{ PatternRow(1, 3) }, jarray{ ShiftRow(1, 1, "Long", 1, 1439, 1440) }, jarray{} };//Monday 23:59 EDT to Tuesday 23:59.
		EXPECT_TRUE( c.At(Utc(2026,7,8,3,58)) ) << "Tuesday 23:58, its last minute";
		EXPECT_FALSE( c.At(Utc(2026,7,8,3,59)) ) << "Tuesday 23:59, its end";
	}

	TEST( ResolveTests, ExceptionsOutsideTheWalkChangeNothing ){
		let c = Plant( jarray{ ExceptionRow(1, 1, "Long ago", Utc(2020,7,1), Utc(2020,7,2)), ExceptionRow(2, 1, "Today", Utc(2026,7,1,12), Utc(2026,7,1,13)) } );
		let day = c.Occurrences( Utc(2026,7,1,10), Utc(2026,7,1,18) );
		ASSERT_EQ( day.size(), 2u );
		ASSERT_EQ( day[1].Breaks.size(), 1u ) << "the uncut piece keeps its break";
		EXPECT_TP( day[1].Breaks[0].Start, Utc(2026,7,1,14) );
	}
}
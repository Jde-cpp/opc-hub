#include "helpers.h"

//The model from the table json and the day arithmetic:  a production day's start, the day of an instant across both clock
//changes, the cycle day from an anchor, the normalised effective, the local-time rule and the re-anchoring.
namespace Jde::Calendar::Tests{
	TEST( DayTests, FromRows ){
		let plant = Plant( jarray{ ExceptionRow(2, 1, "Later", Utc(2026,7,10), Utc(2026,7,11)), ExceptionRow(1, 1, "Earlier", Utc(2026,7,3), Utc(2026,7,4)), ExceptionRow(3, 2, "Another calendar's", Utc(2026,7,1), Utc(2026,7,2)) },
			jarray{ PatternRow(9, 2) }, jarray{ ShiftRow(9, 9, "Another calendar's", Everyday, 0, 60) } );
		EXPECT_EQ( plant.Id(), 1u );
		EXPECT_EQ( plant.Name(), "plant" );
		EXPECT_EQ( plant.Slug(), "plant" );
		EXPECT_EQ( plant.Zone().name(), NewYork );
		EXPECT_EQ( plant.DayStartMinutes(), 360min );
		ASSERT_EQ( plant.Patterns().size(), 1u ) << "the other calendar's pattern is skipped";
		let& pattern = plant.Patterns().front();
		EXPECT_EQ( pattern.Id, 1u );
		EXPECT_FALSE( pattern.Effective );
		EXPECT_EQ( pattern.CycleDays, 7 );
		ASSERT_EQ( pattern.Shifts.size(), 3u );
		EXPECT_EQ( pattern.Shifts[0].Name, "Day" );
		EXPECT_EQ( pattern.Shifts[0].Days, Weekdays );
		ASSERT_EQ( pattern.Shifts[0].Breaks.size(), 1u );
		EXPECT_EQ( pattern.Shifts[0].Breaks[0].Start, 240 );
		EXPECT_EQ( pattern.Shifts[0].Breaks[0].Minutes, 30 );
		EXPECT_EQ( pattern.Shifts[2].Name, "Night" );
		EXPECT_TRUE( pattern.Shifts[2].Breaks.empty() ) << "a null breaks column";
		ASSERT_EQ( plant.Exceptions().size(), 2u ) << "the other calendar's exception is skipped";
		EXPECT_EQ( plant.Exceptions()[0].Name, "Earlier" ) << "by start";
		EXPECT_TP( plant.Exceptions()[0].Start, Utc(2026,7,3) );
		EXPECT_FALSE( plant.Exceptions()[0].Open );
	}

	TEST( DayTests, BreaksColumn ){
		let shifts = jarray{ ShiftRow(1, 1, "A", Everyday, 0, 480, ""), ShiftRow(2, 1, "B", Everyday, 480, 480, jarray{ jobject{{"start", 60}, {"minutes", 15}}, jobject{{"start", 30}, {"minutes", 10}} }) };
		let c = Schedule{ CalendarRow(1, "plant"), jarray{ PatternRow(1, 1) }, shifts, jarray{} };
		let& p = c.Patterns().front();
		EXPECT_TRUE( p.Shifts[0].Breaks.empty() ) << "an empty string, which the db saves as null, is no breaks";
		ASSERT_EQ( p.Shifts[1].Breaks.size(), 2u ) << "an array is taken as it is";
		EXPECT_EQ( p.Shifts[1].Breaks[0].Start, 30 ) << "sorted by start";
		EXPECT_EQ( ParseBreaks(jvalue{nullptr}).size(), 0u );
		EXPECT_EQ( ParseBreaks(jvalue{"[{\"start\":10,\"minutes\":5},{\"start\":0,\"minutes\":5}]"}).size(), 2u ) << "the column's json text";
	}

	TEST( DayTests, UnknownZoneRefused ){
		EXPECT_THROW( (Schedule{ CalendarRow(1, "plant", "Mars/Olympus_Mons"), jarray{}, jarray{}, jarray{} }), Jde::Exception );
	}

	//New York springs forward 2026-03-08 at 02:00 and falls back 2026-11-01 at 02:00.  Under a 06:00 day start, the instants
	//between local midnight and the day's start belong to the day before, on both of those days too.
	TEST( DayTests, DayOfAcrossClockChanges ){
		let plant = Plant();
		EXPECT_TP( plant.DayStart(Ymd(2026,3,7)), Utc(2026,3,7,11) ) << "06:00 EST";
		EXPECT_TP( plant.DayStart(Ymd(2026,3,8)), Utc(2026,3,8,10) ) << "06:00 EDT:  a 23-hour production day before it";
		EXPECT_EQ( plant.DayOf(Utc(2026,3,8,6,30)), Ymd(2026,3,7) ) << "01:30 EST";
		EXPECT_EQ( plant.DayOf(Utc(2026,3,8,7,30)), Ymd(2026,3,7) ) << "03:30 EDT, after the jump";
		EXPECT_EQ( plant.DayOf(Utc(2026,3,8,9,59)), Ymd(2026,3,7) );
		EXPECT_EQ( plant.DayOf(Utc(2026,3,8,10)), Ymd(2026,3,8) );

		EXPECT_TP( plant.DayStart(Ymd(2026,10,31)), Utc(2026,10,31,10) ) << "06:00 EDT";
		EXPECT_TP( plant.DayStart(Ymd(2026,11,1)), Utc(2026,11,1,11) ) << "06:00 EST:  a 25-hour production day before it";
		EXPECT_EQ( plant.DayOf(Utc(2026,11,1,5,30)), Ymd(2026,10,31) ) << "01:30 EDT, the first pass";
		EXPECT_EQ( plant.DayOf(Utc(2026,11,1,6,30)), Ymd(2026,10,31) ) << "01:30 EST, the second";
		EXPECT_EQ( plant.DayOf(Utc(2026,11,1,10,59)), Ymd(2026,10,31) );
		EXPECT_EQ( plant.DayOf(Utc(2026,11,1,11)), Ymd(2026,11,1) );

		EXPECT_EQ( plant.DayOf(Utc(2026,7,1,9,59)), Ymd(2026,6,30) ) << "05:59 EDT on an ordinary day";
		EXPECT_EQ( plant.DayOf(Utc(2026,7,1,10)), Ymd(2026,7,1) );
		EXPECT_TP( plant.Normalize(Utc(2026,7,1,15)), Utc(2026,7,1,10) );
		EXPECT_TP( plant.Normalize(Utc(2026,7,1,9,59)), Utc(2026,6,30,10) ) << "before the day's start is the day before";
	}

	//A day start inside the repeated hour:  the day starts at the first 01:30, and the second pass - and the 01:00 EST between
	//them, earlier on the wall clock than the day's start - is the same day, since its start is not after them.  Inside the
	//skipped hour, the day starts at the instant the clocks jumped to.
	TEST( DayTests, DayStartInsideTheChange ){
		let repeated = Continuous( 90 );
		EXPECT_TP( repeated.DayStart(Ymd(2026,11,1)), Utc(2026,11,1,5,30) ) << "01:30 EDT, the first";
		EXPECT_EQ( repeated.DayOf(Utc(2026,11,1,5,29)), Ymd(2026,10,31) );
		EXPECT_EQ( repeated.DayOf(Utc(2026,11,1,5,30)), Ymd(2026,11,1) );
		EXPECT_EQ( repeated.DayOf(Utc(2026,11,1,6)), Ymd(2026,11,1) ) << "01:00 EST";
		EXPECT_EQ( repeated.DayOf(Utc(2026,11,1,6,30)), Ymd(2026,11,1) ) << "01:30 EST, the second pass";
		EXPECT_EQ( repeated.DayStart(Ymd(2026,11,2))-repeated.DayStart(Ymd(2026,11,1)), 25h );

		let skipped = Continuous( 150 );
		EXPECT_TP( skipped.DayStart(Ymd(2026,3,8)), Utc(2026,3,8,7) ) << "02:30 EST never happens:  03:00 EDT";
		EXPECT_EQ( skipped.DayOf(Utc(2026,3,8,6,59)), Ymd(2026,3,7) );
		EXPECT_EQ( skipped.DayOf(Utc(2026,3,8,7)), Ymd(2026,3,8) );
		EXPECT_EQ( skipped.DayStart(Ymd(2026,3,8))-skipped.DayStart(Ymd(2026,3,7)), 23h+30min );
	}

	//A 14-day cycle anchored on Monday 2026-06-01, given inside that day:  the anchor is moved to the day's start, the days count
	//in local dates, and the cycle repeats backwards from the anchor too.
	TEST( DayTests, CycleDayFromAnchor ){
		let rotation = Schedule{ CalendarRow(3, "rotation"), jarray{ PatternRow(1, 3, {}, 14, Utc(2026,6,1,15)) }, jarray{}, jarray{} };
		let& p = rotation.Patterns().front();
		ASSERT_TRUE( p.CycleStart );
		EXPECT_TP( *p.CycleStart, Utc(2026,6,1,10) ) << "normalised to the production day's start";
		EXPECT_EQ( rotation.CycleDay(Ymd(2026,6,1), p), 0 );
		EXPECT_EQ( rotation.CycleDay(Ymd(2026,6,14), p), 13 );
		EXPECT_EQ( rotation.CycleDay(Ymd(2026,6,15), p), 0 );
		EXPECT_EQ( rotation.CycleDay(Ymd(2026,5,31), p), 13 ) << "the day before the anchor is the cycle's last";
		EXPECT_EQ( rotation.CycleDay(Ymd(2026,5,18), p), 0 );
		EXPECT_EQ( rotation.CycleDay(Ymd(2026,11,2), p), 0 ) << "eleven fortnights on, across the fall change:  no drift";
		EXPECT_EQ( rotation.CycleDay(Ymd(2026,11,12), p), 10 );

		let plant = Plant();
		let& weekly = plant.Patterns().front();
		EXPECT_EQ( plant.CycleDay(Ymd(2026,6,1), weekly), 0 ) << "Monday";
		EXPECT_EQ( plant.CycleDay(Ymd(2026,6,7), weekly), 6 ) << "Sunday";
	}

	//A second pattern from a date given inside the day:  moved to the day's start, so that day is already the new pattern's.
	TEST( DayTests, EffectiveNormalisedAndInEffect ){
		let plant = Plant( {}, jarray{ PatternRow(2, 1, Utc(2026,7,6,15)) }, jarray{ ShiftRow(4, 2, "Day2", Everyday, 60, 480) } );
		ASSERT_EQ( plant.Patterns().size(), 2u );
		EXPECT_EQ( plant.Patterns()[0].Id, 1u ) << "the first pattern first";
		ASSERT_TRUE( plant.Patterns()[1].Effective );
		EXPECT_TP( *plant.Patterns()[1].Effective, Utc(2026,7,6,10) );
		ASSERT_TRUE( plant.InEffect(Ymd(2026,7,5)) );
		EXPECT_EQ( plant.InEffect(Ymd(2026,7,5))->Id, 1u );
		ASSERT_TRUE( plant.InEffect(Ymd(2026,7,6)) );
		EXPECT_EQ( plant.InEffect(Ymd(2026,7,6))->Id, 2u );
		EXPECT_EQ( plant.InEffect(Ymd(2030,1,1))->Id, 2u );
		EXPECT_EQ( plant.InEffect(Ymd(2000,1,1))->Id, 1u ) << "the first pattern is in effect from the beginning of time";

		let late = Schedule{ CalendarRow(1, "late"), jarray{ PatternRow(1, 1, Utc(2026,7,6,10)) }, jarray{}, jarray{} };
		EXPECT_EQ( late.InEffect(Ymd(2026,7,5)), nullptr ) << "nothing in effect before a dated first pattern";
		let tie = Schedule{ CalendarRow(1, "tie"), jarray{ PatternRow(7, 1, Utc(2026,7,6,10)), PatternRow(5, 1, Utc(2026,7,6,10)) }, jarray{}, jarray{} };
		EXPECT_EQ( tie.InEffect(Ymd(2026,7,6))->Id, 5u ) << "the lowest id on a tie";
	}

	//A time with Z or an offset is the instant it names;  one without is local in the calendar's zone.
	TEST( DayTests, LocalTimeRule ){
		let plant = Plant();
		EXPECT_TP( plant.ToTimePoint("2026-07-01T06:00:00"), Utc(2026,7,1,10) ) << "EDT";
		EXPECT_TP( plant.ToTimePoint("2026-01-15T06:00:00"), Utc(2026,1,15,11) ) << "EST";
		EXPECT_TP( plant.ToTimePoint("2026-07-01T06:00:00Z"), Utc(2026,7,1,6) );
		EXPECT_TP( plant.ToTimePoint("2026-07-01T06:00:00+02:00"), Utc(2026,7,1,4) );
		EXPECT_TP( plant.ToTimePoint("2026-03-08T02:30:00"), Utc(2026,3,8,7) ) << "a skipped time is the instant the clocks jumped to";
		EXPECT_TP( plant.ToTimePoint("2026-11-01T01:30:00"), Utc(2026,11,1,5,30) ) << "a repeated time is its first";
		EXPECT_THROW( plant.ToTimePoint("yesterday"), Jde::Exception );
	}

	//A change of zone or day start keeps what the admin entered:  the production day a pattern starts on, and the wall-clock
	//time an exception's bound was entered as.
	TEST( DayTests, Reanchor ){
		let plant = Plant();
		let chicago = Schedule{ 9, "chicago", "chicago", *locate_zone("America/Chicago"), 0 };
		EXPECT_TP( ReanchorDay(Utc(2026,7,6,10), plant, chicago), Utc(2026,7,6,5) ) << "Monday 06:00 EDT names 2026-07-06, whose day starts 00:00 CDT";
		EXPECT_TP( ReanchorLocal(Utc(2026,7,3,10), plant, chicago), Utc(2026,7,3,11) ) << "06:00 EDT re-entered as 06:00 CDT";
		let midnight = Schedule{ 9, "midnight", "midnight", plant.Zone(), 0 };
		EXPECT_TP( ReanchorDay(Utc(2026,7,6,10), plant, midnight), Utc(2026,7,6,4) ) << "the same day under a midnight start";
		EXPECT_TP( ReanchorLocal(Utc(2026,7,3,10), plant, midnight), Utc(2026,7,3,10) ) << "a day start change moves no exception";
		EXPECT_TP( ReanchorLocal(Utc(2026,11,1,6,30), plant, midnight), Utc(2026,11,1,6,30) ) << "nor one in the second pass of the repeated hour";
		EXPECT_TP( ReanchorDay(Utc(2026,11,1,11), plant, midnight), Utc(2026,11,1,4) ) << "the day of the fall change:  its 00:00 is still EDT";
		EXPECT_TP( ReanchorDay(Utc(2026,11,2,11), plant, midnight), Utc(2026,11,2,5) ) << "the day after:  00:00 EST";
	}
}
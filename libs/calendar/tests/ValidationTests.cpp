#include "helpers.h"

//Every refusal of the spec's *Validation* that needs only the row:  each a BadRequest whose message names what was wrong.
namespace Jde::Calendar::Tests{
	#define EXPECT_REFUSED( call, fragment ) { let what = Refused( [&]{ call; } ); EXPECT_NE( what.find(fragment), string::npos ) << #call << " - '" << what << "'"; }

	Ω shift( sv name, uint32 days, uint16 start, uint16 minutes, vector<Break> breaks={} )ι->Shift{ return Shift{ 0, 0, string{name}, days, start, minutes, move(breaks) }; }
	Ω pattern( vector<Shift> shifts, uint8 cycleDays=7, optional<TimePoint> cycleStart={} )ι->Pattern{ return Pattern{ 1, 1, {}, cycleDays, cycleStart, move(shifts) }; }

	TEST( ValidationTests, Calendar ){
		EXPECT_REFUSED( ValidateZone("Mars/Olympus_Mons"), "timeZone 'Mars/Olympus_Mons' is not one the server knows" );
		EXPECT_EQ( ValidateZone(NewYork).name(), NewYork );
		EXPECT_REFUSED( ValidateCalendar(NewYork, 1440), "dayStart 1440 is not under 1440" );
		EXPECT_NO_THROW( ValidateCalendar(NewYork, 1439) );
		EXPECT_NO_THROW( ValidateCalendar("UTC", 0) );
		EXPECT_REFUSED( ValidateCalendar("Nowhere", 0), "timeZone 'Nowhere'" );
	}

	TEST( ValidationTests, Cycle ){
		EXPECT_REFUSED( ValidatePattern(pattern({}, 0)), "cycleDays 0 is not 1 to 31" );
		EXPECT_REFUSED( ValidatePattern(pattern({}, 32, Utc(2026,6,1,10))), "cycleDays 32 is not 1 to 31" );
		EXPECT_REFUSED( ValidatePattern(pattern({}, 14)), "A 14-day cycle needs a cycleStart" );
		EXPECT_NO_THROW( ValidatePattern(pattern({}, 7)) ) << "the default needs no anchor";
		EXPECT_NO_THROW( ValidatePattern(pattern({}, 14, Utc(2026,6,1,10))) );
		EXPECT_NO_THROW( ValidatePattern(pattern({}, 1, Utc(2026,6,1,10))) );
		EXPECT_NO_THROW( ValidatePattern(pattern({}, 31, Utc(2026,6,1,10))) );
	}

	TEST( ValidationTests, ShiftDays ){
		EXPECT_REFUSED( ValidateShift(shift("Day", 128, 0, 480), 7), "Shift 'Day' has days bit 7 set, beyond a 7-day cycle" );
		EXPECT_NO_THROW( ValidateShift(shift("Day", 64, 0, 480), 7) ) << "Sunday";
		EXPECT_NO_THROW( ValidateShift(shift("Day", 0, 0, 480), 7) ) << "a shift on no day is allowed, and never occurs";
		EXPECT_REFUSED( ValidatePattern(pattern({shift("A", 1u<<13, 0, 480)}, 10, Utc(2026,6,1,10))), "bit 13 set, beyond a 10-day cycle" );//cycleDays lowered under a shift's bit
		EXPECT_NO_THROW( ValidatePattern(pattern({shift("A", 1u<<13, 0, 480)}, 14, Utc(2026,6,1,10))) );
	}

	TEST( ValidationTests, ShiftTimes ){
		EXPECT_REFUSED( ValidateShift(shift("Day", Weekdays, 1440, 480), 7), "Shift 'Day' starts at minute 1440" );
		EXPECT_NO_THROW( ValidateShift(shift("Day", Weekdays, 1439, 1440), 7) );
		EXPECT_REFUSED( ValidateShift(shift("Day", Weekdays, 0, 0), 7), "runs 0 minutes, which is not 1 to 1440" );
		EXPECT_REFUSED( ValidateShift(shift("Day", Weekdays, 0, 1441), 7), "runs 1441 minutes" );
		EXPECT_NO_THROW( ValidateShift(shift("Day", Weekdays, 0, 1), 7) );
	}

	TEST( ValidationTests, Breaks ){
		EXPECT_REFUSED( ValidateShift(shift("Day", Weekdays, 0, 480, {{470, 20}}), 7), "break at minute 470 for 20 minutes, which leaves the 480-minute shift" );
		EXPECT_REFUSED( ValidateShift(shift("Day", Weekdays, 0, 480, {{480, 1}}), 7), "leaves the 480-minute shift" );
		EXPECT_REFUSED( ValidateShift(shift("Day", Weekdays, 0, 480, {{100, 0}}), 7), "break at minute 100 with no length" );
		EXPECT_REFUSED( ValidateShift(shift("Day", Weekdays, 0, 480, {{120, 30}, {100, 30}}), 7), "breaks at minutes 100 and 120 that overlap" );//in either order
		EXPECT_NO_THROW( ValidateShift(shift("Day", Weekdays, 0, 480, {{100, 30}, {130, 30}, {450, 30}}), 7) ) << "back to back and to the end";
		EXPECT_REFUSED( ParseBreaks(jvalue{"garbage"}), "breaks must be a json list of {start, minutes}" );
		EXPECT_REFUSED( ParseBreaks(jvalue{"[{\"start\":1}]"}), "breaks must be a json list" );
		EXPECT_REFUSED( ParseBreaks(jvalue{"[{\"start\":-1,\"minutes\":5}]"}), "breaks must be a json list" );
		EXPECT_REFUSED( ParseBreaks(jvalue{7}), "breaks must be a json list" );
	}

	TEST( ValidationTests, ShiftsOverlap ){
		EXPECT_REFUSED( ValidatePattern(pattern({shift("Day", Weekdays, 0, 480), shift("Evening", Weekdays, 470, 480)})), "Shifts 'Day' on Monday and 'Evening' on Monday overlap" );
		EXPECT_NO_THROW( ValidatePattern(pattern({shift("Day", Weekdays, 0, 480), shift("Evening", Weekdays, 480, 480), shift("Night", Weekdays, 960, 480)})) ) << "back to back";
		EXPECT_NO_THROW( ValidatePattern(pattern({shift("Day", 1, 0, 480), shift("Later", 2, 0, 480)})) ) << "the same hours on different days";
		EXPECT_REFUSED( ValidatePattern(pattern({shift("Night", 64, 1380, 480), shift("Day", 1, 0, 480)})), "Shifts 'Night' on Sunday and 'Day' on Monday overlap" );//Sunday's night wraps into Monday
		EXPECT_NO_THROW( ValidatePattern(pattern({shift("Night", 16, 1380, 480), shift("Day", 1, 0, 480)})) ) << "Friday's night ends on Saturday";
		EXPECT_REFUSED( ValidatePattern(pattern({shift("Long", Everyday, 0, 1440), shift("Short", Everyday, 60, 60)})), "overlap" );
		EXPECT_NO_THROW( ValidatePattern(pattern({shift("Long", Everyday, 0, 1440)})) ) << "a day-long shift every day is back to back with itself";
		EXPECT_REFUSED( ValidatePattern(pattern({shift("A", 1, 1380, 480), shift("B", 2, 0, 480)}, 2, Utc(2026,6,1,10))), "Shifts 'A' on day 0 and 'B' on day 1 overlap" );//a rotation names its days by number
		EXPECT_REFUSED( ValidatePattern(pattern({shift("A", 2, 1380, 480), shift("B", 1, 0, 480)}, 2, Utc(2026,6,1,10))), "'A' on day 1 and 'B' on day 0 overlap" );//the last day wraps into the first
	}

	//Night 23:00-07:00 under the old pattern, then a new one from Monday 2026-07-06, on a midnight day start.
	Ω seam( uint32 oldDays, uint32 newDays, uint16 newStart )ε->Schedule{
		return Schedule{ CalendarRow(1, "seam", NewYork, 0), jarray{ PatternRow(1, 1), PatternRow(2, 1, Utc(2026,7,6,4)) }, jarray{ ShiftRow(1, 1, "Night", oldDays, 1380, 480), ShiftRow(2, 2, "Day", newDays, newStart, 480) }, jarray{} };
	}
	TEST( ValidationTests, Seams ){
		EXPECT_REFUSED( ValidateSeams(seam(Everyday, Everyday, 0)), "Shift 'Night' runs 420 minutes into 2026-07-06, the first day of a new pattern, past the start of its shift 'Day'" );
		EXPECT_REFUSED( ValidateSeams(seam(64, 1, 419)), "past the start" );
		EXPECT_NO_THROW( ValidateSeams(seam(Everyday, Everyday, 420)) ) << "back to back";
		EXPECT_NO_THROW( ValidateSeams(seam(Weekdays, Everyday, 0)) ) << "the old shift doesn't run on Sunday";
		EXPECT_NO_THROW( ValidateSeams(seam(Everyday, 2, 0)) ) << "the new shift doesn't run on Monday";
		EXPECT_NO_THROW( ValidateSeams(Plant()) ) << "one pattern has no seam";
	}

	TEST( ValidationTests, Exception ){
		EXPECT_REFUSED( ValidateException(Override{1, 1, "Holiday", Utc(2026,7,3,10), Utc(2026,7,3,10)}), "Exception 'Holiday' ends at 2026-07-03T10:00:00Z, which is not after its start" );
		EXPECT_REFUSED( ValidateException(Override{1, 1, "Holiday", Utc(2026,7,4,10), Utc(2026,7,3,10)}), "not after its start" );
		EXPECT_NO_THROW( ValidateException(Override{1, 1, "Holiday", Utc(2026,7,3,10), Utc(2026,7,3,10,1)}) );
	}
}
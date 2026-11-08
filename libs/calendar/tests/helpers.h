#pragma once
#define let const auto

//The calendars the suite resolves, built from rows as the table QL returns them, in America/New_York:  EDT is UTC-4 from
//2026-03-08 02:00 and EST UTC-5 from 2026-11-01 02:00, so a 06:00 day start is 10:00Z in summer and 11:00Z in winter.
namespace Jde::Calendar::Tests{
	using namespace std::chrono;
	constexpr sv NewYork{ "America/New_York" };
	constexpr uint32 Weekdays{ 31 };//Monday to Friday.
	constexpr uint32 Everyday{ 127 };

	Ξ Utc( int y, unsigned m, unsigned d, int h=0, int min=0 )ι->TimePoint{ return sys_days{year{y}/month{m}/day{d}}+hours{h}+minutes{min}; }
	Ξ Iso( TimePoint t )ι->string{ return ToIsoString<seconds>( t ); }
	Ξ Ymd( int y, unsigned m, unsigned d )ι->year_month_day{ return year{y}/month{m}/day{d}; }
	#define EXPECT_TP( a, b ) EXPECT_EQ( Iso(a), Iso(b) )

	Ξ CalendarRow( uint32 id, sv slug, sv timeZone=NewYork, uint16 dayStart=360 )ι->jobject{
		return jobject{ {"id", id}, {"name", slug}, {"slug", slug}, {"timeZone", timeZone}, {"dayStart", dayStart} };
	}
	Ξ PatternRow( uint32 id, uint32 calendarId, optional<TimePoint> effective={}, uint8 cycleDays=7, optional<TimePoint> cycleStart={} )ι->jobject{
		auto time = []( const optional<TimePoint>& t ){ return t ? jvalue{Iso(*t)} : jvalue{}; };
		return jobject{ {"id", id}, {"calendarId", calendarId}, {"effective", time(effective)}, {"cycleDays", cycleDays}, {"cycleStart", time(cycleStart)} };
	}
	Ξ ShiftRow( uint32 id, uint32 patternId, sv name, uint32 days, uint16 start, uint16 minutes, jvalue breaks=nullptr )ι->jobject{
		return jobject{ {"id", id}, {"calendarPatternId", patternId}, {"name", name}, {"days", days}, {"start", start}, {"minutes", minutes}, {"breaks", move(breaks)} };
	}
	Ξ ExceptionRow( uint32 id, uint32 calendarId, sv name, TimePoint start, TimePoint end, bool open=false )ι->jobject{
		return jobject{ {"id", id}, {"calendarId", calendarId}, {"name", name}, {"start", Iso(start)}, {"end", Iso(end)}, {"open", open} };
	}

	//Day 06:00-14:00 with a half hour at 10:00, Evening 14:00-22:00 and Night 22:00-06:00, Monday to Friday, on a 06:00 day start.
	Ξ PlantShifts( uint32 patternId=1, uint32 firstId=1 )ι->jarray{
		return jarray{ ShiftRow(firstId, patternId, "Day", Weekdays, 0, 480, "[{\"start\":240,\"minutes\":30}]"), ShiftRow(firstId+1, patternId, "Evening", Weekdays, 480, 480), ShiftRow(firstId+2, patternId, "Night", Weekdays, 960, 480) };
	}
	Ξ Plant( jarray exceptions={}, jarray morePatterns={}, jarray moreShifts={} )ε->Calendar{
		jarray patterns{ PatternRow(1, 1) };
		patterns.insert( patterns.end(), morePatterns.begin(), morePatterns.end() );
		auto shifts = PlantShifts();
		shifts.insert( shifts.end(), moreShifts.begin(), moreShifts.end() );
		return Calendar{ CalendarRow(1, "plant"), patterns, shifts, exceptions };
	}
	//Night 22:00-06:00 every day of the week, so a clock-change night is a shift.
	Ξ Continuous( uint16 dayStart=360 )ε->Calendar{
		return Calendar{ CalendarRow(2, "continuous", NewYork, dayStart), jarray{ PatternRow(1, 2) }, jarray{ ShiftRow(1, 1, "Night", Everyday, 960, 480) }, jarray{} };
	}
}
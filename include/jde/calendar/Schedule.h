#pragma once
#include <jde/fwk/chrono.h>

//The shift calendar's model and resolver (docs/calendar/spec.md *Resolution*):  a calendar's rows, as the table QL returns
//them, and the questions about time they answer, in instants, in the calendar's zone.  No database and no QL here.
namespace Jde::Calendar{
	using namespace std::chrono;

	constexpr local_days CycleMonday{ year{1970}/January/5 };//a Monday:  day 0 of a cycle with no anchor, so the default's mask is the weekday's.

	struct Break final{ uint16 Start{}; uint16 Minutes{}; };//minutes from the shift's start.
	struct Shift final{//a calendarShifts row.
		uint32 Id{};
		uint32 PatternId{};
		string Name;
		uint32 Days{};//bit i is cycle day i:  Monday is bit 0 on the default cycle.
		uint16 Start{};//minutes after the production day's start, under 1440.
		uint16 Minutes{};//1 to 1440.
		vector<Break> Breaks;//by Start.
	};
	struct Pattern final{//a calendarPatterns row with its shifts.
		uint32 Id{};
		uint32 CalendarId{};
		optional<TimePoint> Effective;//the start of the production day it is in effect from;  none on a calendar's first pattern.
		uint8 CycleDays{ 7 };
		optional<TimePoint> CycleStart;//the start of the production day that is cycle day 0;  none for a 7-day cycle whose day 0 is Monday.
		vector<Shift> Shifts;//by Start, then Id.
		local_days Anchor{ CycleMonday };//cycle day 0's production day, which a Schedule sets from CycleStart.
	};
	//A calendarExceptions row:  a dated range that closes planned time, a holiday or a shutdown, or opens it, an extra
	//Saturday.  Not `Exception`, which would shadow Jde::Exception throughout this namespace.
	struct Override final{
		uint32 Id{};
		uint32 CalendarId{};
		string Name;
		TimePoint Start;
		TimePoint End;
		bool Open{};
	};
	struct Span final{
		TimePoint Start;
		TimePoint End;
		bool operator==( const Span& )const = default;
	};
	//A shift on a production day, or the part of an open exception no shift covers, cut by the closed exceptions.
	struct Occurrence final{
		optional<uint32> Shift;//the shift row;  none for an open exception.
		string Name;//the shift's, or the exception's.
		year_month_day Day;//the production day.
		TimePoint Start;
		TimePoint End;
		vector<Span> Breaks;//by Start, inside [Start, End).
	};
	struct PlannedTime final{
		vector<Span> Spans;//merged, clipped to the range asked for.
		Duration Total{};
	};
	enum class EBucket : uint8{ Shift, Day, Week, Month };
	using BucketBy = std::variant<EBucket,Duration>;//a Duration cuts from the range's start, PT1M at least.
	struct Bucket final{
		TimePoint Start;
		TimePoint End;
		string Label;//the occurrence's name, the day `2026-03-08`, the ISO week `2026-W10`, the month `2026-03`, or a duration bucket's start.
	};

	α ParseBreaks( const jvalue& breaks, SRCE )ε->vector<Break>;//the column's json text, or its array;  null or empty is none.  BadRequest when it is neither.
	α ParseBucketBy( sv by, SRCE )ε->BucketBy;//shift, day, week, month, or an ISO 8601 duration of a minute or more;  BadRequest otherwise.
	α ToJson( const Occurrence& o )ι->jobject;//{ shift name day start end breaks[{ start end }] }, the calendarOccurrences shape.

	//An immutable snapshot of one calendar.  Every method is a pure function of the rows, so a consumer reads one without a lock.
	struct Schedule final{
		//The calendar's row and the three child tables' rows, as the four flat reads return them, each child table's for
		//every calendar - the rows of other calendars are skipped.  An `effective` or `cycleStart` inside a day is moved back
		//to the start of the production day it falls in.
		Schedule( const jobject& row, const jarray& patterns, const jarray& shifts, const jarray& exceptions, SRCE )ε;
		Schedule( uint32 id, string name, string slug, const time_zone& zone, uint16 dayStart, vector<Pattern> patterns={}, vector<Override> exceptions={} )ι;

		α Id()Ι->uint32{ return _id; }
		α Name()Ι->str{ return _name; }
		α Slug()Ι->str{ return _slug; }
		α Zone()Ι->const time_zone&{ return *_zone; }
		α DayStartMinutes()Ι->minutes{ return _dayStart; }
		α Patterns()Ι->const vector<Pattern>&{ return _patterns; }//by Effective, the first pattern's none first, then Id.
		α Exceptions()Ι->const vector<Override>&{ return _exceptions; }//by Start.

		//When a production day begins:  its local midnight plus dayStart, resolved with choose::earliest, so a start the clocks
		//skip is the instant they jumped to and one they repeat is its first.
		α DayStart( year_month_day day )Ι->TimePoint;
		//The production day of an instant:  the latest day whose start is not after it, so the two agree on a clock-change night.
		α DayOf( TimePoint t )Ι->year_month_day;
		α Normalize( TimePoint t )Ι->TimePoint{ return DayStart( DayOf(t) ); }//the start of the production day t falls in - what an `effective` or `cycleStart` is stored as.
		//A day's place in the pattern's cycle:  the days from cycleStart's production day, in local dates so a clock change
		//moves nothing, modulo cycleDays, positive before the anchor too.  Without an anchor, day 0 is Monday.
		α CycleDay( year_month_day day, const Pattern& pattern )Ι->uint8;
		//The pattern in effect on a production day:  the latest whose Effective is none or not after the day's start, the lowest
		//Id on a tie;  null when no pattern is in effect yet.
		α InEffect( year_month_day day )Ι->const Pattern*;
		//An ISO time with 'Z' or an offset is the instant it names;  one without is a local time here, resolved with choose::earliest.
		α ToTimePoint( string iso, SRCE )Ε->TimePoint;

		//The occurrences overlapping [start, end), whole, by Start:  each pattern's shifts on the days the range touches, the
		//closed exceptions cut from them and the open ones added where nothing else is planned.
		α Occurrences( TimePoint start, TimePoint end )Ι->vector<Occurrence>;
		//The occurrences less their breaks, merged and clipped to [start, end), with the total inside it.
		α Planned( TimePoint start, TimePoint end )Ι->PlannedTime;
		α At( TimePoint t )Ι->optional<Occurrence>;//the occurrence containing t.
		α Next( TimePoint t )Ι->optional<TimePoint>;//the next instant after t at which an occurrence or a break starts or ends;  none when nothing is planned in the next 512 days.
		//[start, end) cut at shift occurrences, production days, Monday-start weeks of them, calendar months of them, or every
		//`by` from start, the first and last clipped to the range.  By shift, time outside an occurrence falls in no bucket.
		α Buckets( TimePoint start, TimePoint end, BucketBy by, SRCE )Ε->vector<Bucket>;
	private:
		α Finish()ι->void;//normalises the dates and sorts, for both constructors.
		α Local( year_month_day day, minutes afterDayStart )Ι->TimePoint;
		uint32 _id;
		string _name;
		string _slug;
		const time_zone* _zone;
		minutes _dayStart;
		vector<Pattern> _patterns;
		vector<Override> _exceptions;
	};

	//What a change of zone or dayStart keeps:  a production day's start names the same day under the new settings, and an
	//instant entered as a local time, an exception's bound, the same wall-clock time in the new zone.
	α ReanchorDay( TimePoint dayStart, const Schedule& from, const Schedule& to )ι->TimePoint;
	α ReanchorLocal( TimePoint t, const Schedule& from, const Schedule& to )ι->TimePoint;
}
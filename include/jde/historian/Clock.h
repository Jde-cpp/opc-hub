#pragma once
#include <chrono>
#include <absl/functional/any_invocable.h>

namespace Jde::Opc::Hist{
	//The historian's only source of the time and of deadlines.  Midnight schedules at a time, so it follows the wall clock;
	//delay, MinTimeInterval and the heartbeat schedule after an interval, so a clock step neither stalls nor bursts them.
	//A test drives both with a manual clock instead of waiting.
	struct IClock{
		using TimerId = uint;
		virtual ~IClock()=default;
		β Now()Ι->TimePoint=0;
		//Runs f once, at or after due.  Never inside Schedule, and never under a lock of the host's - the historian takes
		//its own locks from f.  Once the process's executor is torn down, f is dropped and the id is 0.  f handles its own
		//errors:  SystemClock logs one that escapes as Critical, and a test's clock rethrows it, failing the test.
		β Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId=0;
		//As above, once after has elapsed, whatever the wall clock does meanwhile.
		β Schedule( Duration after, absl::AnyInvocable<void()> f )ι->TimerId=0;
		//true when f had not started and now never will.
		β Cancel( TimerId id )ι->bool=0;
	};
	//Clock::now(), with timers on the process's executor:  a system_timer for a time, a steady_timer for an interval.
	α SystemClock()ι->sp<IClock>;

	//The day t falls on in tz - the day whose file a record with that source time goes in.
	α DayOf( TimePoint t, const std::chrono::time_zone& tz )ι->std::chrono::year_month_day;
	//When day starts in tz:  its local midnight, or the DST change itself when the change skips midnight.  A midnight
	//that occurs twice is its first occurrence.
	α DayStart( std::chrono::year_month_day day, const std::chrono::time_zone& tz )ι->TimePoint;
	α NextDayStart( TimePoint t, const std::chrono::time_zone& tz )ι->TimePoint;
}
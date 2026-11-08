#pragma once
#include "calendar.h"

//The rule checks of docs/calendar/spec.md *Validation* that need only a row, the zone and the ranges.  Each throws a
//Jde::Exception with EHttpStatus::BadRequest whose message is what the client sees;  the mutation await adds the checks
//that need the database.
namespace Jde::Calendar{
	α ValidateZone( sv timeZone, SRCE )ε->const time_zone&;//a name the zone database knows.
	α ValidateCalendar( sv timeZone, uint dayStart, SRCE )ε->const time_zone&;//the zone, and a dayStart under 1440.
	//days bits under cycleDays, a start under 1440, 1 to 1440 minutes, and breaks of a minute or more inside the shift that don't overlap.
	α ValidateShift( const Shift& shift, uint8 cycleDays, SRCE )ε->void;
	//cycleDays 1 to 31, a cycleStart unless the cycle is 7 days, each shift under it, and no two shifts whose occurrences
	//would overlap, checked over the cycle with its wrap into the next.
	α ValidatePattern( const Pattern& pattern, SRCE )ε->void;
	α ValidateException( const Override& exception, SRCE )ε->void;//runs forward.
}
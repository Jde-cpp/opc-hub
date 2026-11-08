#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/ql/QLAwait.h>
#include <jde/calendar/Schedule.h>

namespace Jde::Calendar::Server{
	//A calendar's row and its three child tables' rows:  what the checks, the side effects and the occurrences read.
	struct Rows final{ jobject Calendar; jarray Patterns; jarray Shifts; jarray Exceptions; };
	//A read of the server's own, as System:  not the caller's.
	α SystemRead( string query, sp<QL::IQL> ql, SL sl )ε->QL::QLAwait<jvalue>;
	//Fills rows with the calendar's patterns and exceptions and those patterns' shifts.  liveOnly leaves the deleted rows out;
	//otherwise they come too, each with its `deleted`.
	α ReadChildren( uint32 calendarId, bool liveOnly, sp<QL::IQL> ql, Rows& rows, SL sl )ι->AnyVoidTask;
	//A time argument, read in the calendar's zone:  null or absent is nullopt.
	α FindTimeArg( const QL::Input& q, sv key, const Schedule& c, SL sl )ε->optional<TimePoint>;
}
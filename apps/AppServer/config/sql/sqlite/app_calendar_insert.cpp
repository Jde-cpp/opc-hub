#include "appProcs.h"

//Twins of the *generated* insert procs of the calendar's four tables (app-meta.jsonnet, docs/calendar/spec.md *Tables*) -
//column order and defaults match TableDdl::InsertProcCreateStatement:  insertable columns in `i` order, created=$now, the
//sequence column out last, returned as the result row.
namespace Jde::DB::Sqlite{
	//	params: [0]=_time_zone, [1]=_day_start, [2]=_name, [3]=_slug, [4]=_attributes, [5]=_description; out _calendar_id.
	α AppProcs::RegisterAppCalendarInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "app_calendar_insert", "insert into app_calendars( time_zone, day_start, name, slug, attributes, created, description ) values( ?, ?, ?, ?, ?, unixepoch(), ? )", 6 );
	}
	//	params: [0]=_calendar_id, [1]=_effective, [2]=_cycle_days, [3]=_cycle_start; out _calendar_pattern_id.
	α AppProcs::RegisterAppCalendarPatternInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "app_calendar_pattern_insert", "insert into app_calendar_patterns( calendar_id, effective, cycle_days, cycle_start, created ) values( ?, ?, ?, ?, unixepoch() )", 4 );
	}
	//	params: [0]=_calendar_pattern_id, [1]=_name, [2]=_days, [3]=_start, [4]=_minutes, [5]=_breaks; out _calendar_shift_id.
	α AppProcs::RegisterAppCalendarShiftInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "app_calendar_shift_insert", "insert into app_calendar_shifts( calendar_pattern_id, name, days, start, minutes, breaks ) values( ?, ?, ?, ?, ?, ? )", 6 );
	}
	//	params: [0]=_calendar_id, [1]=_name, [2]=_start, [3]=_finish, [4]=_is_open; out _calendar_exception_id.
	α AppProcs::RegisterAppCalendarExceptionInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "app_calendar_exception_insert", "insert into app_calendar_exceptions( calendar_id, name, start, finish, is_open, created ) values( ?, ?, ?, ?, ?, unixepoch() )", 5 );
	}
}
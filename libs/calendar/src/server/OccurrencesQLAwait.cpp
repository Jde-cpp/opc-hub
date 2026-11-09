#include <jde/calendar/server/OccurrencesQLAwait.h>
#include <jde/fwk/exceptions/refuse.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/calendar/server/CalendarMAwait.h>
#include "reads.h"

#define let const auto

namespace Jde::Calendar::Server{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr days MaxRange{ 366 };//a year, a leap year's too.
	//The years a bound may name:  it parses to any the clock holds, and two far apart overflow the clock's duration when subtracted.
	constexpr TimePoint FirstBound{ std::chrono::sys_days{std::chrono::year{1}/std::chrono::January/1} };
	constexpr TimePoint PastBound{ std::chrono::sys_days{std::chrono::year{10000}/std::chrono::January/1} };

	//The calendar the arguments name, as a filter:  `calendar`, a slug or an id, else `id`.
	Ω calendarFilter( const QL::TableQL& q, SL sl )ε->string{
		if( let p = q.FindPtr<jvalue>("calendar"); p && !p->is_null() ){
			if( p->is_string() )
				return Ƒ( "slug:{}", serialize(*p) );
			if( let id = p->try_to_number<uint32>(); id )
				return Ƒ( "id:{}", *id );
			REFUSE( "calendar is a slug or an id, not {}.", serialize(*p) );
		}
		if( let id = q.FindId<uint32>(); id )
			return Ƒ( "id:{}", *id );
		REFUSE( "'{}' needs the calendar:  its slug, calendar:\"plant\", or its id.", q.JsonName );
	}
	//A bound of the range, read in the calendar's zone.
	Ω requireTime( const QL::TableQL& q, sv key, const Schedule& c, SL sl )ε->TimePoint{
		let t = FindTimeArg( q, key, c, sl );
		REFUSE_IF( !t, "{} is required:  the range's bound, a local time in {} or with Z or an offset.", key, c.Zone().name() );
		REFUSE_IF( *t<FirstBound || *t>=PastBound, "{} is outside the years 1 to 9999.", key );
		return *t;
	}
	//The columns asked for, from an occurrence's json, all of them when none was named.  Not TableQL::TrimColumns, which takes a
	//sub-table as one object:  breaks is a list.
	Ω trim( const QL::TableQL& q, jobject&& full )ι->jobject{
		if( q.Columns.empty() && q.Tables.empty() )
			return move( full );
		jobject y;
		for( let& c : q.Columns ){
			if( auto p = full.if_contains(c.JsonName); p )
				y[c.JsonName] = move( *p );
		}
		for( let& t : q.Tables ){
			auto p = full.if_contains( t.JsonName );
			if( !p )
				continue;
			if( auto items = p->if_array(); items ){
				jarray trimmed;
				for( auto& item : *items )
					trimmed.push_back( item.is_object() ? jvalue{trim(t, move(item.get_object()))} : move(item) );
				y[t.JsonName] = move( trimmed );
			}
			else
				y[t.JsonName] = move( *p );
		}
		return y;
	}

	α OccurrencesQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			let sl = _sl;//REFUSE's.
			DB::AppSchema::GetTablePtr( _ql->Schemas(), string{CalendarTable::Calendars}, _sl )->Authorize( Access::ERights::Read, UserPK(), _sl );//asked first:  a validation message never precedes a refusal.
			_query.CheckVariables( _sl );//before they're substituted:  an unbound one would read as an absent argument.
			_query.Args = _query.ExtrapolateVariables();
			let filter = calendarFilter( _query, _sl );
			auto calendars = co_await SystemRead( Ƒ("calendars( {}, deleted:null ){{ id name slug timeZone dayStart }}", filter), _ql, _sl );
			THROW_IFX( !calendars.is_array() || calendars.get_array().empty(), Exception(_sl, {EHttpStatus::NotFound}, "Calendar {} not found.", filter) );
			let row = Json::AsObject( calendars.get_array().front(), _sl );
			const Schedule settings{ row, jarray{}, jarray{}, jarray{}, _sl };//the zone the bounds are read under.
			let start = requireTime( _query, "start", settings, _sl ), end = requireTime( _query, "end", settings, _sl );
			REFUSE_IF( end<=start, "end {} is not after start {}.", ToIsoString<seconds>(end), ToIsoString<seconds>(start) );
			let& zone = settings.Zone();
			REFUSE_IF( zone.to_local(end)-zone.to_local(start)>MaxRange, "The range {} to {} is over a year:  ask for a year at most.", ToIsoString<seconds>(start), ToIsoString<seconds>(end) );//the calendar's days:  a year to the day across a clock change is an hour over in instants.

			Rows rows;
			co_await ReadChildren( Json::AsNumber<uint32>(row, "id", _sl), true, _ql, rows, _sl );
			const Schedule schedule{ row, rows.Patterns, rows.Shifts, rows.Exceptions, _sl };
			jarray y;
			for( let& occurrence : schedule.Occurrences(start, end) )
				y.push_back( trim(_query, ToJson(occurrence)) );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
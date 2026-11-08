#include <jde/calendar/server/CalendarMAwait.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/IQL.h>
#include <jde/ql/QLAwait.h>
#include <jde/ql/ops/MutationAwait.h>
#include <jde/calendar/validation.h>
#include "../refuse.h"

#define let const auto

namespace Jde{
	constexpr ELogTags _tags{ ELogTags::App };

	namespace Calendar::Server{
		using QL::EMutationQL;
		using namespace CalendarTable;
		//A calendar's row and its three child tables' rows, deleted ones among them - what the checks and the side effects read.
		struct Rows final{ jobject Calendar; jarray Patterns; jarray Shifts; jarray Exceptions; };

		Ω isDeleted( const jvalue& row )ι->bool{
			let p = row.is_object() ? row.get_object().if_contains( "deleted" ) : nullptr;
			return p && !p->is_null();
		}
		Ω live( const jarray& rows )ι->jarray{
			jarray y;
			for( let& r : rows ){
				if( !isDeleted(r) )
					y.push_back( r );
			}
			return y;
		}
		Ω find( jarray& rows, uint32 id )ι->jobject*{
			for( auto& r : rows ){
				if( r.is_object() && Json::FindNumber<uint32>(r.get_object(), "id")==id )
					return &r.get_object();
			}
			return nullptr;
		}
		Ω findTime( const jobject& row, sv key, SL sl )ε->optional<TimePoint>{
			let p = row.if_contains( key );
			return p && !p->is_null() ? optional<TimePoint>{ Chrono::ToTimePoint(Json::AsString(*p, sl), sl) } : nullopt;
		}
		Ω merge( jobject& row, const jobject& args )ι->void{
			for( let& [key,value] : args )
				row[key] = value;
		}
		Ω rowId( const QL::MutationQL& m, SL sl )ε->uint32{
			let id = m.FindId<uint32>();
			REFUSE_IF( !id, "'{}' needs the row's id.", m.CommandName );
			return *id;
		}
		Ω requireParent( const QL::MutationQL& m, sv name, SL sl )ε->uint32{
			let p = m.FindPtr<jvalue>( name );
			REFUSE_IF( !p || p->is_null(), "{} is required, the parent row's id.", name );
			let id = p->try_to_number<uint32>();
			REFUSE_IF( !id, "{} is a row's id, not {}.", name, serialize(*p) );
			return *id;
		}
		//How a calendars mutation names its row, as the stock update keys one:  id, else name, else slug.
		Ω calendarFilter( const QL::MutationQL& m, SL sl )ε->string{
			if( let id = m.FindId<uint32>(); id )
				return Ƒ( "id:{}", *id );
			for( sv key : {"name", "slug"} ){
				if( let p = m.FindPtr<jstring>(key); p )
					return Ƒ( "{}:{}", key, serialize(*p) );
			}
			REFUSE( "'{}' needs the row's id.", m.CommandName );
		}
		//The one row a select by key answered, or NotFound.
		Ω one( jvalue&& result, sv what, sv key, SL sl )ε->jobject{
			auto rows = result.is_array() ? move(result.get_array()) : jarray{};
			THROW_IFX( rows.empty(), Exception(sl, {EHttpStatus::NotFound}, "{} {} not found.", what, key) );
			return Json::AsObject( rows.front(), sl );
		}
		//A column the stock op would type as a whole number, from the merged row:  a refusal here names the column where the engine's
		//would not, and keeps the value inside what the library's row types hold, so its own messages follow.
		Ω wholeNumber( const jobject& row, sv key, sv what, uint32 max, SL sl )ε->void{
			let p = row.if_contains( key );
			REFUSE_IF( !p || p->is_null(), "{} is required, {}.", key, what );
			let n = p->try_to_number<uint32>();
			REFUSE_IF( !n || *n>max, "{} is {}, not {}.", key, what, serialize(*p) );
		}
		Ω requireName( const jobject& row, SL sl )ε->void{
			let p = row.if_contains( "name" );
			REFUSE_IF( !p || !p->is_string() || p->get_string().empty(), "name is required." );
		}
		//A time column named by the mutation, read in the calendar's zone:  null or absent is left alone.
		Ω namedTime( const QL::MutationQL& m, sv key, const Schedule& c, SL sl )ε->optional<TimePoint>{
			let p = m.FindPtr<jvalue>( key );
			if( !p || p->is_null() )
				return nullopt;
			REFUSE_IF( !p->is_string(), "{} is a dateTime, not {}.", key, serialize(*p) );
			try{
				return c.ToTimePoint( string{p->get_string()}, sl );
			}
			catch( const std::exception& e ){
				REFUSE( "{} is not an ISO time:  {}", key, e.what() );
			}
		}
		Ω iso( TimePoint t )ι->jvalue{ return jvalue{ ToIsoString<seconds>(t) }; }
		Ω day( TimePoint t, const Schedule& c )ι->string{ return std::format( "{:%F}", c.DayOf(t) ); }

		//The natural key (calendarId, effective) as the database can't say it:  a second null, which a unique index lets through,
		//and a date a soft-deleted pattern still holds, whose refusal would otherwise name the index.  `all` is every pattern of the
		//calendar, deleted ones among them;  `row` the pattern as it would be.  A deleted pattern holds its date, which the index
		//enforces, but not the null:  the first pattern is the one in effect from the beginning of time, so only a live one counts.
		Ω checkEffective( const jarray& all, const jobject& row, const Schedule& c, SL sl )ε->void{
			let id = Json::FindNumber<uint32>( row, "id" ).value_or( 0 );
			let effective = findTime( row, "effective", sl );
			for( let& v : all ){
				let& other = Json::AsObject( v, sl );
				let otherId = Json::FindNumber<uint32>( other, "id" ).value_or( 0 );
				let deleted = isDeleted( v );
				if( otherId==id || findTime(other, "effective", sl)!=effective || (!effective && deleted) )
					continue;
				if( !effective )
					REFUSE( "Calendar '{}' already has a first pattern, {}:  one pattern has no effective date, in effect from the beginning of time;  give this one the date it takes effect.", c.Name(), otherId );
				REFUSE( "Calendar '{}' already has a pattern effective {}, {}{}.", c.Name(), day(*effective, c), otherId, deleted ? ", deleted:  purge it to reuse the date, or restore it" : "" );
			}
		}
		Ω pattern( const Schedule& c, uint32 id, SL sl )ε->const Pattern&{
			let p = std::ranges::find_if( c.Patterns(), [id]( const Pattern& p ){ return p.Id==id; } );
			THROW_IFSL( p==c.Patterns().end(), "Pattern {} is not among calendar {}'s.", id, c.Id() );
			return *p;
		}
		//Which mutations need the calendar's rows:  the checks and the side effects below.
		Ω needsRows( const QL::MutationQL& m )ι->bool{
			using enum EMutationQL;
			let table = m.TableName();
			if( table==Calendars )
				return m.Type==Purge || (m.Type==Update && (m.Args.contains("timeZone") || m.Args.contains("dayStart")));
			if( table==Patterns )
				return true;
			if( table==Shifts )
				return m.Type!=Purge;
			return m.Type==Create || m.Type==Update;
		}
	}

	α Calendar::Server::CalendarMAwait::IsApplicable( const QL::MutationQL& m )ι->bool{
		using enum EMutationQL;
		let table = m.TableName();
		if( table==Shifts )
			return m.Type==Create || m.Type==Update || m.Type==Purge;
		return (table==Calendars || table==Patterns || table==Exceptions) && (m.Type==Create || m.Type==Update || m.Type==Delete || m.Type==Restore || m.Type==Purge);
	}

	//One coroutine:  the reads are QLAwaits and the writes stock MutationAwaits, both this frame's own task type.
	α Calendar::Server::CalendarMAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			using enum EMutationQL;
			let sl = _sl;//REFUSE's.
			let type = _query.Type;
			let table = _query.TableName();
			_query.DBTable->Authorize( QL::StockRights(type), UserPK(), sl );//asked first:  a validation message never precedes a refusal, and a purge's children go before its statement.
			auto read = [&]( string query ){ return QL::QLAwait<jvalue>{ move(query), jobject{}, Jde::UserPK{Jde::UserPK::System}, _ql, true, _sl }; };//the server's own reads, not the caller's.
			auto stock = [&]( sv command, jobject args ){ return QL::MutationAwait{ QL::MutationQL{string{command}, move(args), ms<jobject>(), nullopt, true, _ql->Schemas(), false}, _creds, nullptr, _sl }; };
			let args = _query.ExtrapolateVariables();

			//The parent chain up to the calendar, then its rows.
			Rows rows;
			optional<uint32> calendarId, patternId;//a pattern's or a shift's parent.
			optional<Schedule> before;//the calendar as it is, live rows only:  the zone and day start the arguments are read under.
			if( needsRows(_query) ){
				string filter;
				if( table==Calendars )
					filter = calendarFilter( _query, _sl );
				else{
					if( table==Patterns ){
						if( type==Create )
							calendarId = requireParent( _query, "calendarId", _sl );
						else
							patternId = rowId( _query, _sl );
					}
					else if( table==Shifts ){
						if( type==Create )
							patternId = requireParent( _query, "calendarPatternId", _sl );
						else{
							let shiftId = rowId( _query, _sl );
							let shift = one( co_await read(Ƒ("calendarShifts( id:{} ){{ id calendarPatternId }}", shiftId)), "Shift", std::to_string(shiftId), _sl );
							patternId = Json::AsNumber<uint32>( shift, "calendarPatternId", _sl );
						}
					}
					else if( type==Create )
						calendarId = requireParent( _query, "calendarId", _sl );
					else{
						let exceptionId = rowId( _query, _sl );
						let exception = one( co_await read(Ƒ("calendarExceptions( id:{} ){{ id calendarId }}", exceptionId)), "Exception", std::to_string(exceptionId), _sl );
						calendarId = Json::AsNumber<uint32>( exception, "calendarId", _sl );
					}
					if( patternId && !calendarId ){
						let pattern = one( co_await read(Ƒ("calendarPatterns( id:{} ){{ id calendarId }}", *patternId)), "Pattern", std::to_string(*patternId), _sl );
						calendarId = Json::AsNumber<uint32>( pattern, "calendarId", _sl );
					}
					filter = Ƒ( "id:{}", *calendarId );
				}
				rows.Calendar = one( co_await read(Ƒ("calendars( {} ){{ id name slug timeZone dayStart }}", filter)), "Calendar", filter, _sl );
				calendarId = Json::AsNumber<uint32>( rows.Calendar, "id", _sl );
				auto children = co_await read( Ƒ("{{ calendarPatterns( calendarId:{0} ){{ id calendarId effective cycleDays cycleStart deleted }} calendarShifts{{ id calendarPatternId name days start minutes breaks }} calendarExceptions( calendarId:{0} ){{ id calendarId name start finish isOpen deleted }} }}", *calendarId) );
				auto& o = Json::AsObject( children, _sl );
				rows.Patterns = move( Json::AsArray(o.at("calendarPatterns"), _sl) );
				rows.Shifts = move( Json::AsArray(o.at("calendarShifts"), _sl) );
				rows.Exceptions = move( Json::AsArray(o.at("calendarExceptions"), _sl) );
				before.emplace( rows.Calendar, live(rows.Patterns), rows.Shifts, live(rows.Exceptions), _sl );
			}

			//The checks, on the row as it would be.  `spelled`:  the columns rewritten here, a time read in the calendar's zone, a
			//date moved to its production day's start, breaks sent as an array, which the result carries over what the caller sent.
			jobject spelled;
			if( table==Calendars ){
				if( type==Create ){
					let zone = _query.FindPtr<jstring>( "timeZone" );
					REFUSE_IF( !zone, "timeZone is required, an IANA name such as America/New_York." );
					ValidateCalendar( *zone, _query.TryNumber<uint>("dayStart").value_or(0), _sl );
				}
				else if( type==Update && before ){
					let zone = _query.FindPtr<jstring>( "timeZone" );
					ValidateCalendar( zone ? sv{*zone} : before->Zone().name(), _query.TryNumber<uint>("dayStart").value_or((uint)before->DayStartMinutes().count()), _sl );
				}
			}
			else if( table==Patterns && type!=Purge ){
				for( sv key : {"effective", "cycleStart"} ){
					if( let t = namedTime(_query, key, *before, _sl); t )
						spelled[key] = iso( before->Normalize(*t) );
				}
				auto patterns = live( rows.Patterns );//in effect once the row is written.
				jobject row;//the pattern as it would be.
				if( type==Create ){
					row = jobject{ {"id", 0}, {"calendarId", *calendarId}, {"effective", nullptr}, {"cycleDays", 7}, {"cycleStart", nullptr} };
					merge( row, args ); merge( row, spelled );
					patterns.push_back( row );
				}
				else{
					let p = find( rows.Patterns, *patternId );
					THROW_IFX( !p, Exception(_sl, {EHttpStatus::NotFound}, "Pattern {} not found.", *patternId) );
					row = *p;
					let wasLive = find( patterns, *patternId )!=nullptr;
					if( type==Delete ){
						REFUSE_IF( wasLive && patterns.size()==1, "Pattern {} is calendar '{}'s only pattern:  a calendar always has one in effect, so add the next before deleting it.", *patternId, before->Name() );
						patterns.erase( std::remove_if(patterns.begin(), patterns.end(), [&]( const jvalue& v ){ return Json::FindNumber<uint32>(v.get_object(), "id")==*patternId; }), patterns.end() );
					}
					else if( type==Update ){
						merge( row, args ); merge( row, spelled );
						if( auto q = find(patterns, *patternId); q )
							*q = row;
					}
					else if( !wasLive )//a restore.
						patterns.push_back( row );
				}
				wholeNumber( row, "cycleDays", "the cycle's length in days", 255, _sl );
				if( type==Create || type==Restore || (type==Update && args.contains("effective")) )
					checkEffective( rows.Patterns, row, *before, _sl );
				if( type!=Delete )//its own rules, from a schedule of the row alone:  an update of a deleted pattern is checked too, in effect or not.
					ValidatePattern( Schedule{rows.Calendar, jarray{row}, rows.Shifts, jarray{}, _sl}.Patterns().front(), _sl );
				ValidateSeams( Schedule{rows.Calendar, patterns, rows.Shifts, jarray{}, _sl}, _sl );
			}
			else if( table==Shifts && type!=Purge ){
				if( let b = _query.FindPtr<jvalue>("breaks"); b && b->is_array() )
					spelled["breaks"] = serialize( *b );
				auto shifts = rows.Shifts;
				jobject row;
				if( type==Create ){
					row = jobject{ {"id", 0}, {"calendarPatternId", *patternId}, {"name", ""}, {"days", 0}, {"start", 0}, {"minutes", 0}, {"breaks", nullptr} };
					merge( row, args ); merge( row, spelled );
					shifts.push_back( row );
				}
				else{
					let s = find( shifts, rowId(_query, _sl) );
					THROW_IFX( !s, Exception(_sl, {EHttpStatus::NotFound}, "Shift {} not found.", rowId(_query, _sl)) );
					merge( *s, args ); merge( *s, spelled );
					row = *s;
				}
				requireName( row, _sl );
				wholeNumber( row, "days", "a bitmask of the cycle days the shift runs", UINT32_MAX, _sl );
				wholeNumber( row, "start", "minutes after the production day's start", UINT16_MAX, _sl );
				wholeNumber( row, "minutes", "the shift's length in minutes", UINT16_MAX, _sl );
				auto patterns = live( rows.Patterns );
				if( !find(patterns, *patternId) ){//a deleted pattern's shift is checked against its own pattern, which is not in effect.
					let p = find( rows.Patterns, *patternId );
					THROW_IFX( !p, Exception(_sl, {EHttpStatus::NotFound}, "Pattern {} not found.", *patternId) );
					patterns.push_back( *p );
				}
				Schedule after{ rows.Calendar, patterns, shifts, jarray{}, _sl };
				ValidatePattern( pattern(after, *patternId, _sl), _sl );
				ValidateSeams( after, _sl );
			}
			else if( table==Exceptions && (type==Create || type==Update) ){
				for( sv key : {"start", "finish"} ){
					if( let t = namedTime(_query, key, *before, _sl); t )
						spelled[key] = iso( *t );
				}
				jobject row;
				if( type==Create )
					row = jobject{ {"id", 0}, {"calendarId", *calendarId}, {"name", ""}, {"start", nullptr}, {"finish", nullptr}, {"isOpen", false} };
				else{
					let e = find( rows.Exceptions, rowId(_query, _sl) );
					THROW_IFX( !e, Exception(_sl, {EHttpStatus::NotFound}, "Exception {} not found.", rowId(_query, _sl)) );
					row = *e;
				}
				merge( row, args ); merge( row, spelled );
				requireName( row, _sl );
				let start = findTime( row, "start", _sl ), finish = findTime( row, "finish", _sl );
				REFUSE_IF( !start || !finish, "start and finish are required:  the range's bounds, as local times in {} or with Z or an offset.", before->Zone().name() );
				ValidateException( Override{ 0, *calendarId, string{Json::FindDefaultSV(row, "name")}, *start, *finish, Json::FindBool(row, "isOpen").value_or(false) }, _sl );
			}
			for( let& [key,value] : spelled )
				_query.Args[key] = value;

			//A purge's children first:  the generic purge is one delete, and the foreign keys are enforced.
			if( type==Purge && table==Calendars ){
				for( let& e : rows.Exceptions )
					co_await stock( "purgeCalendarException", {{"id", Json::AsNumber<uint32>(Json::AsObject(e, _sl), "id", _sl)}} );
				for( let& p : rows.Patterns ){
					let id = Json::AsNumber<uint32>( Json::AsObject(p, _sl), "id", _sl );
					for( let& s : rows.Shifts ){
						if( Json::FindNumber<uint32>(Json::AsObject(s, _sl), "calendarPatternId")==id )
							co_await stock( "purgeCalendarShift", {{"id", Json::AsNumber<uint32>(Json::AsObject(s, _sl), "id", _sl)}} );
					}
					co_await stock( "purgeCalendarPattern", {{"id", id}} );
				}
			}
			else if( type==Purge && table==Patterns ){
				for( let& s : rows.Shifts ){
					if( Json::FindNumber<uint32>(Json::AsObject(s, _sl), "calendarPatternId")==*patternId )
						co_await stock( "purgeCalendarShift", {{"id", Json::AsNumber<uint32>(Json::AsObject(s, _sl), "id", _sl)}} );
				}
			}

			jvalue y = co_await QL::MutationAwait{ QL::MutationQL{_query}, _creds, nullptr, _sl };//the stock op:  no QL, so nothing routes it back here.
			auto row0 = y.is_array() && y.get_array().size() ? y.get_array()[0].if_object() : nullptr;

			if( table==Calendars && type==Create ){//ready for its shifts:  the first pattern, in effect from the beginning of time, seven days from Monday.
				let id = row0 ? Json::FindNumber<uint32>( *row0, "id" ).value_or( 0 ) : 0;
				THROW_IFSL( !id, "'{}' answered no id:  {}", _query.CommandName, serialize(y) );
				co_await stock( "createCalendarPattern", {{"calendarId", id}} );
			}
			else if( table==Calendars && type==Update && before ){
				//What the admin entered stays put:  a production day's start names the same day under the new settings, and an exception's
				//bound the same wall-clock time in the new zone.  Every row, deleted ones too, so a restore comes back right.
				auto settings = rows.Calendar;
				merge( settings, args );
				Schedule after{ settings, jarray{}, jarray{}, jarray{}, _sl };
				if( &before->Zone()!=&after.Zone() || before->DayStartMinutes()!=after.DayStartMinutes() ){
					for( let& v : rows.Patterns ){
						let& p = Json::AsObject( v, _sl );
						jobject set;
						for( sv key : {"effective", "cycleStart"} ){
							if( let t = findTime(p, key, _sl); t && ReanchorDay(*t, *before, after)!=*t )
								set[key] = iso( ReanchorDay(*t, *before, after) );
						}
						if( set.size() ){
							set["id"] = Json::AsNumber<uint32>( p, "id", _sl );
							co_await stock( "updateCalendarPattern", move(set) );
						}
					}
					for( let& v : rows.Exceptions ){
						let& e = Json::AsObject( v, _sl );
						jobject set;
						for( sv key : {"start", "finish"} ){
							if( let t = findTime(e, key, _sl); t && ReanchorLocal(*t, *before, after)!=*t )
								set[key] = iso( ReanchorLocal(*t, *before, after) );
						}
						if( set.size() ){
							set["id"] = Json::AsNumber<uint32>( e, "id", _sl );
							co_await stock( "updateCalendarException", move(set) );
						}
					}
				}
			}
			if( row0 ){
				for( let& [key,value] : spelled )
					(*row0)[key] = value;
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
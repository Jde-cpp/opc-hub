#include <jde/calendar/server/CalendarMAwait.h>
#include <jde/fwk/exceptions/refuse.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ops/MutationAwait.h>
#include <jde/calendar/validation.h>
#include "../rows.h"
#include "reads.h"

#define let const auto

namespace Jde{
	constexpr ELogTags _tags{ ELogTags::App };

	namespace Calendar::Server{
		using QL::EMutationQL;
		using namespace CalendarTable;

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
		//Takes the row of `id` out, true when it was there.
		Ω removeRow( jarray& rows, uint32 id )ι->bool{
			let p = std::remove_if( rows.begin(), rows.end(), [id]( const jvalue& v ){ return Json::FindNumber<uint32>(v.get_object(), "id")==id; } );
			let found = p!=rows.end();
			rows.erase( p, rows.end() );
			return found;
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
		//dayStart as the arguments name it, else `fallback`.
		Ω namedDayStart( const jobject& args, uint fallback, SL sl )ε->uint{
			if( !args.contains("dayStart") )
				return fallback;
			wholeNumber( args, "dayStart", "minutes after local midnight", UINT16_MAX, sl );
			return Json::AsNumber<uint>( args, "dayStart", sl );
		}
		Ω requireName( const jobject& row, SL sl )ε->void{
			let p = row.if_contains( "name" );
			REFUSE_IF( !p || !p->is_string() || p->get_string().empty(), "name is required." );
		}
		Ω iso( TimePoint t )ι->jvalue{ return jvalue{ ToIsoString<seconds>(t) }; }
		Ω day( TimePoint t, const Schedule& c )ι->string{ return std::format( "{:%F}", c.DayOf(t) ); }

		//The natural key (calendarId, effective) as the database can't say it:  a second null, which MySQL's and sqlite's unique
		//indexes let through and SQL Server's refuses naming itself, and a date a soft-deleted pattern still holds, whose refusal
		//would otherwise name the index.  `all` is every pattern of the calendar, deleted ones among them;  `row` the pattern as it
		//would be.  A deleted pattern holds its date and its null alike until its purge.
		Ω checkEffective( const jarray& all, const jobject& row, const Schedule& c, SL sl )ε->void{
			let id = Json::FindNumber<uint32>( row, "id" ).value_or( 0 );
			let effective = findTime( row, "effective", sl );
			for( let& v : all ){
				let& other = Json::AsObject( v, sl );
				let otherId = Json::FindNumber<uint32>( other, "id" ).value_or( 0 );
				let deleted = isDeleted( v );
				if( otherId==id || findTime(other, "effective", sl)!=effective )
					continue;
				if( !effective )
					REFUSE( "Calendar '{}' already has a first pattern, {}{}", c.Name(), otherId, deleted ? ", deleted:  purge it to make another the first, or restore it." : ":  one pattern has no effective date, in effect from the beginning of time;  give this one the date it takes effect." );
				REFUSE( "Calendar '{}' already has a pattern effective {}, {}{}.", c.Name(), day(*effective, c), otherId, deleted ? ", deleted:  purge it to reuse the date, or restore it" : "" );
			}
		}
		Ω pattern( const Schedule& c, uint32 id, SL sl )ε->const Pattern&{
			let p = std::ranges::find_if( c.Patterns(), [id]( const Pattern& p ){ return p.Id==id; } );
			THROW_IFSL( p==c.Patterns().end(), "Pattern {} is not among calendar {}'s.", id, c.Id() );
			return *p;
		}
		//The order to re-anchor patterns in.  The unique (calendar_id, effective) applies row by row, and a move of a whole number of days
		//lands a date on the one the next pattern still holds.  Days keep their order, so a row moving later can only meet another moving
		//later:  those go from the last back, then the rest from the first on.
		Ω reanchorOrder( const jarray& patterns, const Schedule& before, const Schedule& after, SL sl )ε->vector<const jobject*>{
			vector<const jobject*> y;
			for( let& v : patterns )
				y.push_back( &Json::AsObject(v, sl) );
			let effective = [&]( const jobject* p ){ return findTime( *p, "effective", sl ); };
			std::ranges::sort( y, {}, effective );
			let rest = std::ranges::stable_partition( y, [&]( const jobject* p ){ let t = effective( p ); return t && ReanchorDay(*t, before, after)>*t; } );
			std::reverse( y.begin(), rest.begin() );
			return y;
		}
		//One row's rewrite under new settings:  its id and the columns that move, empty when none does.
		Ω rewrite( const jobject& row, std::initializer_list<sv> keys, const auto& reanchor, SL sl )ε->jobject{
			jobject set;
			for( sv key : keys ){
				if( let t = findTime(row, key, sl); t && reanchor(*t)!=*t )
					set[key] = iso( reanchor(*t) );
			}
			if( set.size() )
				set["id"] = Json::AsNumber<uint32>( row, "id", sl );
			return set;
		}
		//What the admin entered stays put:  a production day's start names the same day under the new settings.  Every row, deleted
		//ones too, so a restore comes back right.
		Ω reanchorPatterns( const jarray& patterns, const Schedule& before, const Schedule& after, SL sl )ε->vector<jobject>{
			vector<jobject> y;
			for( let p : reanchorOrder(patterns, before, after, sl) ){
				if( auto set = rewrite(*p, {"effective", "cycleStart"}, [&]( TimePoint t ){ return ReanchorDay(t, before, after); }, sl); set.size() )
					y.push_back( move(set) );
			}
			return y;
		}
		//An exception's bound stays the same wall-clock time in the new zone.
		Ω reanchorExceptions( const jarray& exceptions, const Schedule& before, const Schedule& after, SL sl )ε->vector<jobject>{
			vector<jobject> y;
			for( let& v : exceptions ){
				if( auto set = rewrite(Json::AsObject(v, sl), {"start", "finish"}, [&]( TimePoint t ){ return ReanchorLocal(t, before, after); }, sl); set.size() )
					y.push_back( move(set) );
			}
			return y;
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
		if( m.CommandName==CopyCommand )
			return true;
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
			_query.CheckVariables( _sl );//the stock op's check, which comes after those children.
			auto stock =[&]( sv command, jobject args ){ return QL::MutationAwait{ QL::MutationQL{string{command}, move(args), ms<jobject>(), nullopt, true, _ql->Schemas(), false}, _creds, nullptr, _sl }; };
			let args = _query.ExtrapolateVariables();

			//The parent chain up to the calendar, then its rows.
			Rows rows;
			optional<uint32> calendarId, patternId;//a pattern's or a shift's parent.
			optional<Schedule> before;//the calendar as it is, live rows only:  the zone and day start the arguments are read under.
			if( needsRows(_query) ){
				string filter;
				if( table==Calendars ){
					if( type==Purge )
						rowId( _query, _sl );//the stock purge takes an id alone, and the children go before it.
					filter = calendarFilter( _query, _sl );
				}
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
							let shift = one( co_await SystemRead(Ƒ("calendarShifts( id:{} ){{ id calendarPatternId }}", shiftId), _ql, _sl), "Shift", std::to_string(shiftId), _sl );
							patternId = Json::AsNumber<uint32>( shift, "calendarPatternId", _sl );
						}
					}
					else if( type==Create )
						calendarId = requireParent( _query, "calendarId", _sl );
					else{
						let exceptionId = rowId( _query, _sl );
						let exception = one( co_await SystemRead(Ƒ("calendarExceptions( id:{} ){{ id calendarId }}", exceptionId), _ql, _sl), "Exception", std::to_string(exceptionId), _sl );
						calendarId = Json::AsNumber<uint32>( exception, "calendarId", _sl );
					}
					if( patternId && !calendarId ){
						let pattern = one( co_await SystemRead(Ƒ("calendarPatterns( id:{} ){{ id calendarId }}", *patternId), _ql, _sl), "Pattern", std::to_string(*patternId), _sl );
						calendarId = Json::AsNumber<uint32>( pattern, "calendarId", _sl );
					}
					filter = Ƒ( "id:{}", *calendarId );
				}
				rows.Calendar = one( co_await SystemRead(Ƒ("calendars( {} ){{ id name slug timeZone dayStart }}", filter), _ql, _sl), "Calendar", filter, _sl );
				calendarId = Json::AsNumber<uint32>( rows.Calendar, "id", _sl );
				co_await ReadChildren( *calendarId, false, _ql, rows, _sl );
				before.emplace( rows.Calendar, live(rows.Patterns), rows.Shifts, live(rows.Exceptions), _sl );
			}

			//The checks, on the row as it would be.  `spelled`:  the columns rewritten here, a time read in the calendar's zone, a
			//date moved to its production day's start, breaks sent as an array, which the result carries over what the caller sent.
			jobject spelled;
			if( table==Calendars ){
				if( type==Create ){
					let zone = _query.FindPtr<jstring>( "timeZone" );
					REFUSE_IF( !zone, "timeZone is required, an IANA name such as America/New_York." );
					ValidateCalendar( *zone, namedDayStart(args, 0, _sl), _sl );
				}
				else if( type==Update && before ){
					let zone = _query.FindPtr<jstring>( "timeZone" );
					REFUSE_IF( !zone && args.contains("timeZone"), "timeZone is an IANA name such as America/New_York, not {}.", serialize(args.at("timeZone")) );
					ValidateCalendar( zone ? sv{*zone} : before->Zone().name(), namedDayStart(args, (uint)before->DayStartMinutes().count(), _sl), _sl );
				}
			}
			else if( table==Patterns && type!=Purge ){
				for( sv key : {"effective", "cycleStart"} ){
					if( let t = FindTimeArg(_query, key, *before, _sl); t )
						spelled[key] = iso( before->Normalize(*t) );
				}
				auto patterns = live( rows.Patterns );//in effect once the row is written.
				jobject row;//the pattern as it would be.
				if( type==Create ){
					row = jobject{ {"id", 0}, {"calendarId", *calendarId}, {"effective", nullptr}, {"cycleDays", 7}, {"cycleStart", nullptr} };
					merge( row, args ); merge( row, spelled );
					row["id"] = 0;//the sequence's:  a caller's would pass for the row it names.
					patterns.push_back( row );
				}
				else{
					let p = find( rows.Patterns, *patternId );
					THROW_IFX( !p, Exception(_sl, {EHttpStatus::NotFound}, "Pattern {} not found.", *patternId) );
					row = *p;
					let wasLive = find( patterns, *patternId )!=nullptr;
					if( type==Delete ){
						REFUSE_IF( wasLive && patterns.size()==1, "Pattern {} is calendar '{}'s only pattern:  a calendar always has one in effect, so add the next before deleting it.", *patternId, before->Name() );
						removeRow( patterns, *patternId );
					}
					else if( type==Update ){
						merge( row, args ); merge( row, spelled );
						row["calendarId"] = *calendarId;//not updateable, which the stock op refuses:  a row of another calendar is in no schedule of this one.
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
			else if( table==Patterns ){//a purge:  a live pattern's joins its neighbours, as a delete's does.
				auto patterns = live( rows.Patterns );
				if( removeRow(patterns, *patternId) )
					ValidateSeams( Schedule{rows.Calendar, patterns, rows.Shifts, jarray{}, _sl}, _sl );
			}
			else if( table==Shifts && type!=Purge ){
				if( let b = _query.FindPtr<jvalue>("breaks"); b && b->is_array() )
					spelled["breaks"] = serialize( *b );
				auto shifts = rows.Shifts;
				jobject row;
				if( type==Create ){
					row = jobject{ {"id", 0}, {"calendarPatternId", *patternId}, {"name", ""}, {"days", nullptr}, {"start", nullptr}, {"minutes", nullptr}, {"breaks", nullptr} };
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
				let inEffect = find( patterns, *patternId )!=nullptr;
				if( !inEffect ){//a deleted pattern's shift is checked against its own pattern alone:  it meets no seam until its restore.
					let p = find( rows.Patterns, *patternId );
					THROW_IFX( !p, Exception(_sl, {EHttpStatus::NotFound}, "Pattern {} not found.", *patternId) );
					patterns.push_back( *p );
				}
				Schedule after{ rows.Calendar, patterns, shifts, jarray{}, _sl };
				ValidatePattern( pattern(after, *patternId, _sl), _sl );
				if( inEffect )
					ValidateSeams( after, _sl );
			}
			else if( table==Exceptions && (type==Create || type==Update) ){
				for( sv key : {"start", "finish"} ){
					if( let t = FindTimeArg(_query, key, *before, _sl); t )
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

			vector<jobject> patternMoves, exceptionMoves;
			if( table==Calendars && type==Update && before ){
				auto settings = rows.Calendar;
				merge( settings, args );
				Schedule after{ settings, jarray{}, jarray{}, jarray{}, _sl };
				if( &before->Zone()!=&after.Zone() || before->DayStartMinutes()!=after.DayStartMinutes() ){
					patternMoves = reanchorPatterns( rows.Patterns, *before, after, _sl );
					exceptionMoves = reanchorExceptions( rows.Exceptions, *before, after, _sl );
				}
			}
			//The side effects run as the caller:  their tables' rights asked before the first write, so a refusal leaves nothing half done.
			let authorize = [&]( sv child, EMutationQL op, bool writes ){
				if( writes )
					_query.DBTable->Schema->GetTable( string{child}, sl ).Authorize( QL::StockRights(op), UserPK(), sl );
			};
			let purgesCalendar = type==Purge && table==Calendars;
			let purgesShifts = purgesCalendar ? rows.Shifts.size()>0 : type==Purge && table==Patterns && std::ranges::any_of( rows.Shifts, [&]( const jvalue& s ){ return Json::FindNumber<uint32>(Json::AsObject(s, sl), "calendarPatternId")==*patternId; } );
			authorize( Patterns, Create, table==Calendars && type==Create );
			authorize( Patterns, Update, patternMoves.size()>0 );
			authorize( Exceptions, Update, exceptionMoves.size()>0 );
			authorize( Exceptions, Purge, purgesCalendar && rows.Exceptions.size()>0 );
			authorize( Patterns, Purge, purgesCalendar && rows.Patterns.size()>0 );
			authorize( Shifts, Purge, purgesShifts );

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
			for( auto& set : patternMoves )
				co_await stock( "updateCalendarPattern", move(set) );
			for( auto& set : exceptionMoves )
				co_await stock( "updateCalendarException", move(set) );
			if( row0 ){
				for( let& [key,value] : spelled )
					(*row0)[key] = value;
			}
			else if( y.is_number() && spelled.size() )//an update's row count:  these columns answer, and the caller's arguments fill in the rest.
				y = move( spelled );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α Calendar::Server::CalendarMAwait::Suspend()ι->void{
		if( _query.CommandName==CopyCommand )
			Copy();
		else
			Query();
	}

	//copyCalendarPattern( id, effective ):  the pattern `id` and its shifts, copied to a pattern in effect from `effective` (spec
	//*Validation*, *Web*).  The pattern and then each shift are created through the host's QL, which routes them back here, so each
	//row meets the checks a caller's would - the date moved to its day's start, the natural key's refusals, the seams.  A shift's
	//refusal purges the copy through this await's purge, the shifts it got first, as System:  the server undoing its own half-done
	//work, which a caller without Purge could not.  A deleted source copies too:  its hours come back as a live pattern.
	α Calendar::Server::CalendarMAwait::Copy()ι->TAwait<jvalue>::Task{
		try{
			let sl = _sl;//REFUSE's.
			let& schemas = _ql->Schemas();
			for( sv table : {Patterns, Shifts} )//first:  a validation message never precedes a refusal.
				DB::AppSchema::GetTablePtr( schemas, string{table}, _sl )->Authorize( Access::ERights::Create, UserPK(), _sl );
			_query.CheckVariables( _sl );
			let sourceId = rowId( _query, _sl );
			let effective = _query.FindPtr<jvalue>( "effective" );
			REFUSE_IF( !effective || effective->is_null(), "effective is required:  the day the copy takes effect, a local time in the calendar's zone or with Z or an offset." );
			auto routed =[&]( sv command, jobject args, QL::Creds creds ){ return QL::MutationAwait{ QL::MutationQL{string{command}, move(args), ms<jobject>(), nullopt, true, schemas, false}, move(creds), _ql, _sl }; };
			let source = one( co_await SystemRead(Ƒ("calendarPatterns( id:{} ){{ id calendarId cycleDays cycleStart }}", sourceId), _ql, _sl), "Pattern", std::to_string(sourceId), _sl );
			auto shiftRows = co_await SystemRead( Ƒ("calendarShifts( calendarPatternId:{} ){{ id name days start minutes breaks }}", sourceId), _ql, _sl );
			let shifts = shiftRows.is_array() ? move( shiftRows.get_array() ) : jarray{};

			auto created = co_await routed( "createCalendarPattern", jobject{ {"calendarId", source.at("calendarId")}, {"effective", *effective}, {"cycleDays", source.at("cycleDays")}, {"cycleStart", source.at("cycleStart")} }, _creds );
			auto pattern = created.is_array() && created.get_array().size() ? Json::AsObject( created.get_array().front(), _sl ) : jobject{};
			let newId = Json::FindNumber<uint32>( pattern, "id" ).value_or( 0 );
			THROW_IFSL( !newId, "createCalendarPattern answered no id:  {}", serialize(created) );
			std::exception_ptr refused;//a shift's, rethrown once the copy is undone:  no co_await in a handler.
			for( let& v : shifts ){
				try{
					let& s = Json::AsObject( v, _sl );
					jobject args{ {"calendarPatternId", newId} };
					for( sv key : {"name", "days", "start", "minutes", "breaks"} )
						args[key] = s.at( key );
					co_await routed( "createCalendarShift", move(args), _creds );
				}
				catch( const runtime_error& ){
					refused = std::current_exception();
					break;
				}
			}
			if( refused ){
				try{
					co_await routed( "purgeCalendarPattern", jobject{{"id", newId}}, QL::Creds{Jde::UserPK{Jde::UserPK::System}} );
				}
				catch( const std::exception& e ){
					ERR( "Pattern {}, the copy of {}, could not be undone after its shift was refused:  {}", newId, sourceId, e.what() );
				}
				std::rethrow_exception( refused );
			}
			for( sv key : {"calendarId", "cycleDays", "cycleStart"} ){//the create answers its id and the columns it spelled;  the rest are the source's.
				if( !pattern.contains(key) )
					pattern[key] = source.at( key );
			}
			Resume( _query.ResultRequest ? jvalue{_query.ResultRequest->TrimColumns(pattern)} : jvalue{move(pattern)} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
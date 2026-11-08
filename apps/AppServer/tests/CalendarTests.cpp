#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/LocalSubscriptions.h>
#include <jde/app/IApp.h>
#include <jde/calendar/Schedule.h>
#include <jde/calendar/validation.h>
#include "helpers.h"
#define let const auto

//The calendar tables (config/app-meta.jsonnet) and CalendarMAwait (libs/calendar/src/server), through the AppServer's own QL as
//the web's forms and /graphql reach them:  the tables sync, the side effects land, a zone-less time is read in the calendar's
//zone, and each refusal of docs/calendar/spec.md *Validation* that needs the database comes back BadRequest with the library's
//message;  then the calendar's own operations, calendarOccurrences and copyCalendarPattern, and the zone on status (*Reads*,
//*Open questions* #8).  America/New_York:  EDT is UTC-4 in July, so a 06:00 day start is 10:00Z.
namespace Jde::App::Server::Tests{
	using namespace std::chrono;
	constexpr sv NewYork{ "America/New_York" };
	constexpr uint32 Weekdays{ 31 };

	//A table's resource, enforced from Enforce() to the end of the scope, a throw included:  the mysql run keeps its db.
	struct ScopedEnforcement final{
		ScopedEnforcement( sv slug )ε{
			let resources = Json::AsArray( RunQL(Ƒ(R"(resources( schemaName:"{}", slug:"{}", criteria:null ){{ id deleted }})", Server::AppSchema()->Name, slug)) );
			let& row = resources.at( 0 );
			_id = Id( row );
			WasEnforced = row.as_object().at( "deleted" ).is_null();
		}
		~ScopedEnforcement(){
			if( !_enforced )
				return;
			try{
				RunQL( Ƒ("deleteResource( id:{} )", _id) );
			}
			catch( const std::exception& e ){
				ADD_FAILURE() << e.what();
			}
		}
		α Enforce()ε->void{
			if( WasEnforced )
				return;
			RunQL( Ƒ("restoreResource( id:{} )", _id) );
			_enforced = true;
		}
		bool WasEnforced{};
	private:
		uint _id{};
		bool _enforced{};
	};

	struct CalendarTests : ::testing::Test{
		//A calendar of `slug`, created anew:  one an earlier run left behind - the mysql run keeps its db - is purged first.
		Ω Fresh( sv slug, sv zone=NewYork, uint dayStart=360 )ε->uint{
			for( let& v : RunQL( Ƒ("calendars( slug:\"{}\" ){{ id }}", slug) ).as_array() )
				RunQL( Ƒ("purgeCalendar( id:{} )", Id(v)) );
			return Id( RunQL(Ƒ("createCalendar( name:\"{0}\", slug:\"{0}\", timeZone:\"{1}\", dayStart:{2} ){{ id }}", slug, zone, dayStart)) );
		}
		Ω Patterns( uint calendarId, bool deleted=false )ε->jarray{ return RunQL( Ƒ("calendarPatterns( calendarId:{}{} ){{ id effective cycleDays cycleStart deleted }}", calendarId, deleted ? "" : ", deleted:null") ).as_array(); }
		Ω Shifts( uint patternId )ε->jarray{ return RunQL( Ƒ("calendarShifts( calendarPatternId:{} ){{ id name days start minutes breaks }}", patternId) ).as_array(); }
		Ω Exceptions( uint calendarId )ε->jarray{ return RunQL( Ƒ("calendarExceptions( calendarId:{} ){{ id name start finish isOpen deleted }}", calendarId) ).as_array(); }
		Ω FirstPattern( uint calendarId )ε->uint{ let patterns = Patterns( calendarId ); return patterns.size() ? Id( patterns.front() ) : 0; }
		Ω Shift( uint patternId, sv name, uint32 days, uint start, uint minutes, sv breaks={} )ε->uint{
			return Id( RunQL(Ƒ("createCalendarShift( calendarPatternId:{}, name:\"{}\", days:{}, start:{}, minutes:{}{} ){{ id }}", patternId, name, days, start, minutes, breaks.size() ? Ƒ(", breaks:{}", serialize(jvalue{breaks})) : string{})) );
		}
		//The calendar as the library reads it, from the four flat reads of spec *Reads*, live rows only.
		Ω Library( uint calendarId )ε->Calendar::Schedule{
			let row = RunQL( Ƒ("calendars( id:{} ){{ id name slug timeZone dayStart }}", calendarId) ).as_array().at( 0 ).as_object();
			let patterns = RunQL( Ƒ("calendarPatterns( calendarId:{}, deleted:null ){{ id calendarId effective cycleDays cycleStart }}", calendarId) ).as_array();
			jarray shifts;
			for( let& p : patterns ){
				for( auto& s : RunQL(Ƒ("calendarShifts( calendarPatternId:{} ){{ id calendarPatternId name days start minutes breaks }}", Id(p))).as_array() )
					shifts.push_back( move(s) );
			}
			return { row, patterns, shifts, RunQL(Ƒ("calendarExceptions( calendarId:{}, deleted:null ){{ id calendarId name start finish isOpen }}", calendarId)).as_array() };
		}
		Ω Time( const jvalue& row, sv key )ε->TimePoint{ return Chrono::ToTimePoint( Json::AsString(Json::AsObject(row), key) ); }
		Ω Utc( int y, unsigned m, unsigned d, int h=0, int min=0 )ι->TimePoint{ return sys_days{year{y}/month{m}/day{d}}+hours{h}+minutes{min}; }
		//What f was refused with, its status and message;  None and empty when it wasn't.
		Ω Refused( function<void()> f )->std::pair<EHttpStatus,string>{
			try{
				f();
			}
			catch( const Exception& e ){
				return { e.HttpStatus(), e.what() };
			}
			return { EHttpStatus::None, {} };
		}
	};
	#define EXPECT_REFUSED( call, fragment ) { let [status, what] = Refused( [&]{ call; } ); EXPECT_EQ( status, EHttpStatus::BadRequest ) << #call << " - '" << what << "'"; EXPECT_NE( what.find(fragment), string::npos ) << #call << " - '" << what << "'"; }
	#define EXPECT_TP( a, b ) EXPECT_EQ( ToIsoString<seconds>(a), ToIsoString<seconds>(b) )

	//The four tables are the app schema's, synced with the rest, and the generic select serves them.
	TEST_F( CalendarTests, TablesSync ){
		for( sv table : {"calendars", "calendar_patterns", "calendar_shifts", "calendar_exceptions"} )
			EXPECT_TRUE( Server::AppSchema()->FindTable(string{table}) ) << table;
		EXPECT_TRUE( RunQL("calendars{ id }").is_array() );
		EXPECT_TRUE( RunQL("calendarShifts{ id }").is_array() );
	}

	//A calendar's create is followed by its first pattern:  no effective, seven days from Monday, so it is ready for its shifts.
	TEST_F( CalendarTests, CreateMakesTheFirstPattern ){
		let calendarId = Fresh( "calFirstPattern" );
		let patterns = Patterns( calendarId );
		ASSERT_EQ( patterns.size(), 1u );
		let& p = patterns.front().as_object();
		EXPECT_TRUE( p.at("effective").is_null() );
		EXPECT_EQ( Json::AsNumber<uint>(p, "cycleDays"), 7u );
		EXPECT_TRUE( p.at("cycleStart").is_null() );
	}

	//Each refusal is a BadRequest with the library's message, and nothing is written.
	TEST_F( CalendarTests, Refusals ){
		EXPECT_REFUSED( RunQL(R"(createCalendar( name:"calBadZone", slug:"calBadZone", timeZone:"Mars/Olympus_Mons" ){ id })"), "timeZone 'Mars/Olympus_Mons' is not one the server knows" );
		EXPECT_TRUE( RunQL(R"(calendars( slug:"calBadZone" ){ id })").as_array().empty() ) << "a refused create writes nothing";
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendar( name:"calBadDay", slug:"calBadDay", timeZone:"{}", dayStart:1440 ){{ id }})", NewYork)), "dayStart 1440 is not under 1440" );
		EXPECT_REFUSED( RunQL(R"(createCalendar( name:"calNoZone", slug:"calNoZone" ){ id })"), "timeZone is required" );
		for( sv bad : {"-5", "360.5", "\"360\""} )
			EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendar( name:"calBadDay", slug:"calBadDay", timeZone:"{}", dayStart:{} ){{ id }})", NewYork, bad)), "dayStart is minutes after local midnight" );

		let calendarId = Fresh( "calRefusals" );
		let patternId = FirstPattern( calendarId );
		ASSERT_NE( patternId, 0u );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendar( id:{}, timeZone:"Nowhere" ))", calendarId)), "timeZone 'Nowhere'" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendar( id:{}, dayStart:1500 ))", calendarId)), "dayStart 1500" );
		EXPECT_REFUSED( RunQL(Ƒ("updateCalendar( id:{}, dayStart:-5 )", calendarId)), "dayStart is minutes after local midnight" );
		EXPECT_REFUSED( RunQL(Ƒ("updateCalendar( id:{}, timeZone:5 )", calendarId)), "timeZone is an IANA name" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T00:00:00", cycleDays:14 ){{ id }})", calendarId)), "A 14-day cycle needs a cycleStart" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T00:00:00", cycleDays:0 ){{ id }})", calendarId)), "cycleDays 0 is not 1 to 31" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{} ){{ id }})", calendarId)), "already has a first pattern" );//a second null effective, which the unique index would let through.
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, id:{} ){{ id }})", calendarId, patternId)), "already has a first pattern" );//a caller's id doesn't pass for the row it names.
		let otherId = Fresh( "calRefusalsOther" );
		EXPECT_REFUSED( RunQL(Ƒ("updateCalendarPattern( id:{}, calendarId:{} )", patternId, otherId)), "isn't updateable" );
		RunQL( Ƒ("purgeCalendar( id:{} )", otherId) );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"garbage" ){{ id }})", calendarId)), "effective is not an ISO time" );
		EXPECT_EQ( Refused([&]{ RunQL(R"(createCalendarPattern( calendarId:999999 ){ id })"); }).first, EHttpStatus::NotFound );
		EXPECT_EQ( Patterns(calendarId, true).size(), 1u ) << "no refused pattern was written";

		let day = Shift( patternId, "Day", Weekdays, 0, 480, R"([{"start":240,"minutes":30}])" );
		ASSERT_NE( day, 0u );
		EXPECT_REFUSED( Shift(patternId, "Overlap", Weekdays, 470, 480), "Shifts 'Day' on Monday and 'Overlap' on Monday overlap" );
		EXPECT_REFUSED( Shift(patternId, "Late", Weekdays, 1440, 60), "starts at minute 1440" );
		EXPECT_REFUSED( Shift(patternId, "Long", 96, 0, 1441), "runs 1441 minutes" );
		EXPECT_REFUSED( Shift(patternId, "Sunday", 128, 600, 60), "days bit 7 set, beyond a 7-day cycle" );
		EXPECT_REFUSED( Shift(patternId, "Breaks", 96, 600, 60, R"([{"start":50,"minutes":20}])"), "leaves the 60-minute shift" );
		EXPECT_REFUSED( Shift(patternId, "Garbage", 96, 600, 60, "garbage"), "breaks must be a json list" );
		EXPECT_REFUSED( RunQL(Ƒ("updateCalendarShift( id:{}, minutes:0 )", day)), "runs 0 minutes" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"NoDays", start:600, minutes:60 ){{ id }})", patternId)), "days is required" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"NoStart", days:96, minutes:60 ){{ id }})", patternId)), "start is required" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"NoLength", days:96, start:600 ){{ id }})", patternId)), "minutes is required" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendarShift( id:{}, start:"noon" ))", day)), "start is minutes after the production day's start, not \"noon\"" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendarPattern( id:{}, cycleDays:3 ))", patternId)), "A 3-day cycle needs a cycleStart" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendarPattern( id:{}, cycleDays:3, cycleStart:"2026-07-06T06:00:00" ))", patternId)), "beyond a 3-day cycle" );//lowered under the shift's bits.
		EXPECT_EQ( Shifts(patternId).size(), 1u ) << "no refused shift was written";

		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarException( calendarId:{}, name:"Backwards", start:"2026-07-04T06:00:00", finish:"2026-07-04T06:00:00" ){{ id }})", calendarId)), "not after its start" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarException( calendarId:{}, name:"Half", start:"2026-07-04T06:00:00" ){{ id }})", calendarId)), "start and finish are required" );
		EXPECT_TRUE( Exceptions(calendarId).empty() );

		EXPECT_REFUSED( RunQL(Ƒ("deleteCalendarPattern( id:{} )", patternId)), "only pattern" );
		EXPECT_EQ( Patterns(calendarId).size(), 1u ) << "the only pattern stays";
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//A time without a zone is a local time in the calendar's zone;  one with Z or an offset is the instant it names.
	TEST_F( CalendarTests, LocalTimesResolveInTheZone ){
		let calendarId = Fresh( "calLocalTimes" );
		let created = RunQL( Ƒ(R"(createCalendarException( calendarId:{}, name:"Independence Day", start:"2026-07-03T18:00:00", finish:"2026-07-05T06:00:00-05:00" ){{ id start finish }})", calendarId) );
		EXPECT_TP( Time(created, "start"), Utc(2026,7,3,22) ) << "the result carries the resolved times";
		let exceptions = Exceptions( calendarId );
		ASSERT_EQ( exceptions.size(), 1u );
		EXPECT_TP( Time(exceptions.front(), "start"), Utc(2026,7,3,22) ) << "18:00 EDT";
		EXPECT_TP( Time(exceptions.front(), "finish"), Utc(2026,7,5,11) ) << "06:00 at -05:00, as given";
		EXPECT_FALSE( Json::AsObject(exceptions.front()).at("isOpen").as_bool() );

		RunQL( Ƒ(R"(updateCalendarException( id:{}, finish:"2026-07-06T06:00:00Z", isOpen:true ))", Id(exceptions.front())) );
		let updated = Exceptions( calendarId );
		EXPECT_TP( Time(updated.front(), "finish"), Utc(2026,7,6,6) );
		EXPECT_TRUE( Json::AsObject(updated.front()).at("isOpen").as_bool() );
		let answered = RunQL( Ƒ(R"(updateCalendarException( id:{}, start:"2026-07-03T19:00:00" ){{ start }})", Id(exceptions.front())) );
		EXPECT_TP( Time(answered, "start"), Utc(2026,7,3,23) ) << "an update's result carries the resolved time too";
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendarException( id:{}, finish:"2026-07-03T10:00:00" ))", Id(exceptions.front()))), "not after its start" );//checked against the stored start.
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//An effective or cycleStart inside a day is moved back to the start of the production day it falls in, read in the zone.
	TEST_F( CalendarTests, EffectiveIsMovedToTheDaysStart ){
		let calendarId = Fresh( "calEffective" );
		let created = RunQL( Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T15:00:00Z", cycleDays:14, cycleStart:"2026-07-01T12:00:00" ){{ id effective cycleStart }})", calendarId) );
		EXPECT_TP( Time(created, "effective"), Utc(2026,7,6,10) ) << "15:00Z Monday is 11:00 EDT, inside Monday's production day, which began at 06:00 EDT";
		EXPECT_TP( Time(created, "cycleStart"), Utc(2026,7,1,10) ) << "noon local, moved back to the day's start";
		let patterns = Patterns( calendarId );
		ASSERT_EQ( patterns.size(), 2u );
		EXPECT_TP( Time(patterns.back(), "effective"), Utc(2026,7,6,10) );
		let moved = RunQL( Ƒ(R"(updateCalendarPattern( id:{}, effective:"2026-07-13T02:00:00" ){{ effective }})", Id(created)) );//02:00 local Monday is still Sunday's production day.
		EXPECT_TP( Time(moved, "effective"), Utc(2026,7,12,10) ) << "an update's result carries the moved date";
		EXPECT_TP( Time(Patterns(calendarId).back(), "effective"), Utc(2026,7,12,10) );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//A change of timeZone or dayStart re-anchors every pattern's dates to the same production day and every exception's bounds
	//to the same wall-clock time, so what was entered stays put.
	TEST_F( CalendarTests, ZoneChangeReanchors ){
		let calendarId = Fresh( "calReanchor" );
		let patternId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00" ){{ id }})", calendarId)) );
		let exceptionId = Id( RunQL(Ƒ(R"(createCalendarException( calendarId:{}, name:"Shutdown", start:"2026-07-03T18:00:00", finish:"2026-07-05T06:00:00" ){{ id }})", calendarId)) );
		RunQL( Ƒ("deleteCalendarException( id:{} )", exceptionId) );//a deleted row is re-anchored too, so a restore comes back right.

		RunQL( Ƒ(R"(updateCalendar( id:{}, timeZone:"Europe/London" ))", calendarId) );
		auto patterns = Patterns( calendarId );
		ASSERT_EQ( patterns.size(), 2u );
		EXPECT_TP( Time(patterns.back(), "effective"), Utc(2026,7,6,5) ) << "Monday 06:00 BST";
		EXPECT_TRUE( Json::AsObject(patterns.front()).at("effective").is_null() ) << "the first pattern has no date to move";
		auto exceptions = Exceptions( calendarId );
		ASSERT_EQ( exceptions.size(), 1u );
		EXPECT_TP( Time(exceptions.front(), "start"), Utc(2026,7,3,17) ) << "Friday 18:00 BST";
		EXPECT_TP( Time(exceptions.front(), "finish"), Utc(2026,7,5,5) );

		RunQL( Ƒ("updateCalendar( id:{}, dayStart:0 )", calendarId) );
		patterns = Patterns( calendarId );
		EXPECT_TP( Time(patterns.back(), "effective"), Utc(2026,7,5,23) ) << "Monday's day now begins at midnight BST";
		exceptions = Exceptions( calendarId );
		EXPECT_TP( Time(exceptions.front(), "start"), Utc(2026,7,3,17) ) << "an exception's bound is a wall-clock time, which the day start doesn't move";

		RunQL( Ƒ(R"(updateCalendar( id:{}, description:"renamed only" ))", calendarId) );
		EXPECT_TP( Time(Patterns(calendarId).back(), "effective"), Utc(2026,7,5,23) ) << "an update that leaves the settings alone moves nothing";
		EXPECT_EQ( patternId, Id(patterns.back()) );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//Apia, UTC+13, to UTC-11 moves every date exactly one day, onto the one the next pattern still holds.  Both ways.
	TEST_F( CalendarTests, WholeDayZoneChangeReanchors ){
		let calendarId = Fresh( "calReanchorDay", "Pacific/Apia", 0 );
		for( sv day : {"2026-07-06", "2026-07-07", "2026-07-08"} )
			RunQL( Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"{}T00:00:00" ){{ id }})", calendarId, day) );
		let expectFrom = [&]( unsigned firstDay ){
			let patterns = Patterns( calendarId );
			ASSERT_EQ( patterns.size(), 4u );
			for( uint i=1; i<patterns.size(); ++i )
				EXPECT_TP( Time(patterns[i], "effective"), Utc(2026, 7, firstDay+(unsigned)i-1, 11) );
		};
		expectFrom( 5 );

		RunQL( Ƒ(R"(updateCalendar( id:{}, timeZone:"Etc/GMT+11" ))", calendarId) );
		expectFrom( 6 );
		RunQL( Ƒ(R"(updateCalendar( id:{}, timeZone:"Pacific/Apia" ))", calendarId) );
		expectFrom( 5 );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//A purge takes the row's children first:  a pattern's shifts, a calendar's exceptions, patterns and their shifts.
	TEST_F( CalendarTests, PurgeTakesTheChildren ){
		let calendarId = Fresh( "calPurge" );
		let patternId = FirstPattern( calendarId );
		Shift( patternId, "Day", Weekdays, 0, 480 );
		Shift( patternId, "Night", Weekdays, 960, 480 );
		let secondId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00" ){{ id }})", calendarId)) );
		Shift( secondId, "Day", Weekdays, 0, 600 );
		RunQL( Ƒ(R"(createCalendarException( calendarId:{}, name:"Holiday", start:"2026-07-03T06:00:00", finish:"2026-07-04T06:00:00" ){{ id }})", calendarId) );

		RunQL( Ƒ("purgeCalendarPattern( id:{} )", secondId) );
		EXPECT_TRUE( Shifts(secondId).empty() ) << "a pattern's purge takes its shifts";
		EXPECT_EQ( Patterns(calendarId).size(), 1u );
		EXPECT_EQ( Shifts(patternId).size(), 2u ) << "and no other's";

		EXPECT_REFUSED( RunQL(R"(purgeCalendar( slug:"calPurge" ))"), "needs the row's id" );
		EXPECT_NE( Refused([&]{ RunQL(Ƒ("purgeCalendar( id:{}, note:$typo )", calendarId)); }).second.find("typo"), string::npos );
		EXPECT_EQ( Shifts(patternId).size(), 2u ) << "a refused purge takes no child";

		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
		EXPECT_TRUE( RunQL(Ƒ("calendars( id:{} ){{ id }}", calendarId)).as_array().empty() );
		EXPECT_TRUE( Patterns(calendarId, true).empty() );
		EXPECT_TRUE( Shifts(patternId).empty() );
		EXPECT_TRUE( Exceptions(calendarId).empty() );
	}

	//A soft-deleted pattern keeps its (calendarId, effective) until its purge, as a deleted group keeps its slug, and the refusal
	//says so;  a delete that leaves the calendar a pattern is allowed, and a restore is checked as a create is.
	TEST_F( CalendarTests, DeletedPatternKeepsItsDate ){
		let calendarId = Fresh( "calDeletedDate" );
		let firstId = FirstPattern( calendarId );
		let create = Ƒ( R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00" ){{ id }})", calendarId );
		let oldId = Id( RunQL(create) );
		EXPECT_REFUSED( RunQL(create), "already has a pattern effective 2026-07-06, " + std::to_string(oldId) + "." );
		RunQL( Ƒ("deleteCalendarPattern( id:{} )", oldId) );
		EXPECT_EQ( Patterns(calendarId).size(), 1u );
		EXPECT_REFUSED( RunQL(create), "purge it to reuse the date, or restore it" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendarPattern( id:{}, effective:"2026-07-06T06:00:00" ))", firstId)), "purge it to reuse the date" );//an update onto the date is the same refusal.
		EXPECT_REFUSED( RunQL(Ƒ("deleteCalendarPattern( id:{} )", firstId)), "only pattern" );//the deleted one isn't in effect, so it doesn't count.

		RunQL( Ƒ("restoreCalendarPattern( id:{} )", oldId) );
		EXPECT_EQ( Patterns(calendarId).size(), 2u );
		RunQL( Ƒ("deleteCalendarPattern( id:{} )", firstId) );//allowed now:  the restored one stays in effect.
		EXPECT_EQ( Patterns(calendarId).size(), 1u );
		RunQL( Ƒ("restoreCalendarPattern( id:{} )", firstId) );

		RunQL( Ƒ("deleteCalendarPattern( id:{} )", oldId) );
		RunQL( Ƒ("purgeCalendarPattern( id:{} )", oldId) );
		let newId = Id( RunQL(create) );
		EXPECT_NE( newId, oldId );
		EXPECT_EQ( Patterns(calendarId).size(), 2u );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//A deleted first pattern holds the null until its purge, as a deleted pattern holds its date.
	TEST_F( CalendarTests, DeletedFirstPatternHoldsTheNull ){
		let calendarId = Fresh( "calDeletedFirst" );
		let firstId = FirstPattern( calendarId );
		let secondId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00" ){{ id }})", calendarId)) );
		RunQL( Ƒ("deleteCalendarPattern( id:{} )", firstId) );
		EXPECT_REFUSED( RunQL(Ƒ("updateCalendarPattern( id:{}, effective:null )", secondId)), "purge it to make another the first, or restore it" );
		EXPECT_REFUSED( RunQL(Ƒ("createCalendarPattern( calendarId:{} ){{ id }}", calendarId)), "purge it to make another the first, or restore it" );

		RunQL( Ƒ("purgeCalendarPattern( id:{} )", firstId) );
		RunQL( Ƒ("updateCalendarPattern( id:{}, effective:null )", secondId) );
		EXPECT_TRUE( Patterns(calendarId).front().as_object().at("effective").is_null() );
		EXPECT_REFUSED( RunQL(Ƒ("createCalendarPattern( calendarId:{} ){{ id }}", calendarId)), "give this one the date it takes effect" );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//Monday's night shift runs 420 minutes into Tuesday.  A purge joins a pattern's neighbours as a delete does.
	TEST_F( CalendarTests, PurgeChecksTheSeamItLeaves ){
		let calendarId = Fresh( "calPurgeSeam" );
		Shift( FirstPattern(calendarId), "Night", 1, 1380, 480 );
		let middleId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00" ){{ id }})", calendarId)) );
		let lastId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-07T06:00:00" ){{ id }})", calendarId)) );
		let earlyId = Shift( lastId, "Early", 2, 0, 60 );
		EXPECT_REFUSED( RunQL(Ƒ("deleteCalendarPattern( id:{} )", middleId)), "runs 420 minutes into 2026-07-07" );
		EXPECT_REFUSED( RunQL(Ƒ("purgeCalendarPattern( id:{} )", middleId)), "runs 420 minutes into 2026-07-07" );
		EXPECT_EQ( Patterns(calendarId).size(), 3u );

		RunQL( Ƒ("purgeCalendarShift( id:{} )", earlyId) );
		RunQL( Ƒ("purgeCalendarPattern( id:{} )", middleId) );
		EXPECT_EQ( Patterns(calendarId).size(), 2u );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//A deleted pattern is in effect nowhere, so its shifts meet no seam until its restore asks.
	TEST_F( CalendarTests, DeletedPatternsShiftMeetsNoSeam ){
		let calendarId = Fresh( "calDeletedSeam" );
		Shift( FirstPattern(calendarId), "Night", 1, 1380, 480 );
		let deletedId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-07T06:00:00" ){{ id }})", calendarId)) );
		RunQL( Ƒ("deleteCalendarPattern( id:{} )", deletedId) );
		Shift( deletedId, "Early", 2, 0, 60 );
		EXPECT_EQ( Shifts(deletedId).size(), 1u );
		EXPECT_REFUSED( RunQL(Ƒ("restoreCalendarPattern( id:{} )", deletedId)), "runs 420 minutes into 2026-07-07" );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//The side effects run as the caller, whose rights on their tables are asked before the first write.  calendarPatterns enforced,
	//calendars not:  the mutation's own table passes, and a refusal leaves nothing half done.
	TEST_F( CalendarTests, ChildRightsAreAskedFirst ){
		let calendarId = Fresh( "calRights" );
		RunQL( Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00" ){{ id }})", calendarId) );
		let resources = Json::AsArray( RunQL(Ƒ(R"(resources( schemaName:"{}", slug:"calendarPatterns", criteria:null ){{ id deleted }})", Server::AppSchema()->Name)) );
		ASSERT_EQ( resources.size(), 1u );
		let resourceId = Id( resources[0] );
		let wasEnforced = resources[0].as_object().at( "deleted" ).is_null();
		if( !wasEnforced )
			RunQL( Ƒ("restoreResource( id:{} )", resourceId) );
		let user = User( "calRightsUser" );

		EXPECT_EQ( Status([&]{ RunQL(Ƒ(R"(createCalendar( name:"calRightsNew", slug:"calRightsNew", timeZone:"{}" ){{ id }})", NewYork), {}, user); }), EHttpStatus::Forbidden );
		EXPECT_TRUE( RunQL(R"(calendars( slug:"calRightsNew" ){ id })").as_array().empty() ) << "no pattern-less calendar";
		EXPECT_EQ( Status([&]{ RunQL(Ƒ(R"(updateCalendar( id:{}, timeZone:"Europe/London" ))", calendarId), {}, user); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Json::AsSV(RunQL(Ƒ("calendars( id:{} ){{ id timeZone }}", calendarId)).as_array()[0].as_object(), "timeZone"), NewYork ) << "the zone wasn't saved";
		EXPECT_EQ( Status([&]{ RunQL(Ƒ("purgeCalendar( id:{} )", calendarId), {}, user); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Patterns(calendarId).size(), 2u ) << "no child went";

		if( !wasEnforced )
			RunQL( Ƒ("deleteResource( id:{} )", resourceId) );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	struct CalendarListener final : QL::IListener{
		CalendarListener()ι: QL::IListener{"CalendarTests"}{}
		α OnChange( const jvalue& j, QL::SubscriptionId )ε->void override{ std::unique_lock _{ Mutex }; Changes.push_back( Json::AsObject(j) ); }
		std::mutex Mutex;
		vector<jobject> Changes;
	};
	//The stock mutation's fan-out is the calendar's:  a listener on the four tables' events hears a shift's update, and that alone.
	TEST_F( CalendarTests, ListenerHearsAShiftUpdate ){
		let calendarId = Fresh( "calListener" );
		let patternId = FirstPattern( calendarId );
		let shiftId = Shift( patternId, "Day", Weekdays, 0, 480 );
		auto listener = ms<CalendarListener>();
		uint i{};
		for( sv table : {"calendar", "calendarPattern", "calendarShift", "calendarException"} ){
			let capitalized = Ƒ( "{}{}", (char)std::toupper(table[0]), table.substr(1) );
			auto await = Server::QLPtr()->Subscribe( Ƒ("subscription {0}Updated{{ {1}Updated(subscriptionId:$id){{ id name }} }}", capitalized, table), jobject{{"id", ++i}}, listener, Jde::UserPK{Jde::UserPK::System} );
			BlockAwait( move(*await) );
		}
		RunQL( Ƒ(R"(updateCalendarShift( id:{}, name:"Days" ))", shiftId) );
		QL::Subscriptions::StopListen( listener );
		std::unique_lock _{ listener->Mutex };
		ASSERT_EQ( listener->Changes.size(), 1u );
		let& change = listener->Changes.front();
		ASSERT_TRUE( change.contains("calendar_shifts") ) << serialize( change );//keyed by the table, as the fan-out spells it.
		let& row = Json::AsObject( change.at("calendar_shifts") );
		EXPECT_EQ( Json::AsNumber<uint>(row, "id"), shiftId );
		EXPECT_EQ( Json::AsSV(row, "name"), "Days" );
		_.unlock();
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//calendarOccurrences answers Schedule::Occurrences from the server's own rows:  a seeded week with a holiday agrees with the
	//library over the same rows, by slug and by id, and the columns asked for are the ones answered, breaks' included.
	TEST_F( CalendarTests, OccurrencesAgreeWithTheLibrary ){
		let calendarId = Fresh( "calOccurrences" );
		let patternId = FirstPattern( calendarId );
		Shift( patternId, "Day", Weekdays, 0, 480, R"([{"start":240,"minutes":30}])" );
		Shift( patternId, "Night", Weekdays, 960, 480 );
		RunQL( Ƒ(R"(createCalendarException( calendarId:{}, name:"Independence Day", start:"2026-07-03T06:00:00", finish:"2026-07-04T06:00:00" ){{ id }})", calendarId) );
		let schedule = Library( calendarId );
		let expected = schedule.Occurrences( schedule.ToTimePoint("2026-06-29T00:00:00"), schedule.ToTimePoint("2026-07-06T00:00:00") );
		ASSERT_EQ( expected.size(), 8u ) << "Monday to Thursday's Day and Night, Friday's two under the holiday";

		let answered = RunQL( R"(calendarOccurrences( calendar:"calOccurrences", start:"2026-06-29T00:00:00", end:"2026-07-06T00:00:00" ){ shift name day start end breaks{ start end } })" ).as_array();
		ASSERT_EQ( answered.size(), expected.size() );
		for( uint i=0; i<expected.size(); ++i )
			EXPECT_EQ( answered[i], jvalue{Calendar::ToJson(expected[i])} ) << i;
		EXPECT_EQ( answered[0].as_object().at("breaks").as_array().size(), 1u ) << "Day's break";

		let byId = RunQL( Ƒ(R"(calendarOccurrences( id:{}, start:"2026-06-29T00:00:00", end:"2026-07-06T00:00:00" ){{ name breaks{{ start }} }})", calendarId) ).as_array();
		ASSERT_EQ( byId.size(), expected.size() );
		EXPECT_EQ( byId[0].as_object().size(), 2u ) << "the columns asked for:  " << serialize( byId[0] );
		EXPECT_EQ( Json::AsSV(byId[0].as_object(), "name"), "Day" );
		EXPECT_EQ( byId[0].as_object().at("breaks").as_array()[0].as_object().size(), 1u ) << "a break's too";
		let byNumber = RunQL( Ƒ(R"(calendarOccurrences( calendar:{}, start:"2026-06-29T00:00:00Z", end:"2026-07-06T00:00:00Z" ){{ name }})", calendarId) ).as_array();
		EXPECT_EQ( byNumber.size(), expected.size() ) << "calendar: takes an id too;  the Z bounds move the range four hours into Sunday night, which is planned nowhere";
		let whole = RunQL( R"(calendarOccurrences( calendar:"calOccurrences", start:"2026-06-29T00:00:00", end:"2026-07-06T00:00:00" ))" ).as_array();
		ASSERT_EQ( whole.size(), expected.size() );
		EXPECT_EQ( whole[0].as_object().size(), 6u ) << "no columns named is every column";
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//Each refusal of the read is a BadRequest naming the argument;  a calendar not found, or deleted, is NotFound;  and a year to
	//the day is answered, a leap year's too.
	TEST_F( CalendarTests, OccurrencesRefusals ){
		let calendarId = Fresh( "calOccurrencesRefused" );
		let query = []( sv args ){ return Ƒ( "calendarOccurrences( {} ){{ name }}", args ); };
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"2026-01-01T00:00:00Z", end:"2027-01-03T00:00:00Z")")), "is over a year" );//367 days.
		EXPECT_TRUE( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"2028-01-01T00:00:00Z", end:"2029-01-01T00:00:00Z")")).is_array() ) << "366 days, a leap year's";
		EXPECT_TRUE( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"2027-11-06T00:00:00", end:"2028-11-06T00:00:00")")).is_array() ) << "366 local days, an hour more in instants:  EDT to EST";
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"-150000-01-01T00:00:00Z", end:"150000-01-01T00:00:00Z")")), "start is outside the years" );//further apart than the clock's duration holds.
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"2026-07-06T00:00:00Z", end:"150000-01-01T00:00:00Z")")), "end is outside the years" );
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"-150000-01-01T00:00:00", end:"2026-07-06T00:00:00Z")")), "outside the years 1 to 9999" );//a local one, refused before the zone reads it.
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"999999-01-01T00:00:00", end:"2026-07-06T00:00:00Z")")), "outside the years 1 to 9999" );//past what the clock holds:  to_sys wouldn't come back.
		EXPECT_NE( Refused([&]{ RunQL(Ƒ(R"(calendarOccurrences( calendar:$typo, id:{}, start:"2026-07-06T00:00:00Z", end:"2026-07-07T00:00:00Z" ){{ name }})", calendarId)); }).second.find("typo"), string::npos ) << "an unbound calendar is not an absent one";
		EXPECT_NE( Refused([&]{ RunQL(query(R"(calendar:"calOccurrencesRefused", start:$typo, end:"2026-07-06T00:00:00Z")")); }).second.find("typo"), string::npos );
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"2026-07-06T00:00:00Z", end:"2026-07-06T00:00:00Z")")), "end 2026-07-06T00:00:00Z is not after start" );
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"2026-07-06T00:00:00Z")")), "end is required" );
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:"yesterday", end:"2026-07-06T00:00:00Z")")), "start is not an ISO time" );
		EXPECT_REFUSED( RunQL(query(R"(calendar:"calOccurrencesRefused", start:7, end:"2026-07-06T00:00:00Z")")), "start is a dateTime, not 7" );
		EXPECT_REFUSED( RunQL(query(R"(start:"2026-07-06T00:00:00Z", end:"2026-07-07T00:00:00Z")")), "needs the calendar" );
		EXPECT_REFUSED( RunQL(query(R"(calendar:true, start:"2026-07-06T00:00:00Z", end:"2026-07-07T00:00:00Z")")), "calendar is a slug or an id, not true" );
		EXPECT_EQ( Refused([&]{ RunQL(query(R"(calendar:"calNoSuch", start:"2026-07-06T00:00:00Z", end:"2026-07-07T00:00:00Z")")); }).first, EHttpStatus::NotFound );
		//Read on calendars:  unenforced, the table passes anyone;  enforced, a user without the grant is refused and an anonymous caller is no known user.
		let week = query( R"(calendar:"calOccurrencesRefused", start:"2026-07-06T00:00:00Z", end:"2026-07-07T00:00:00Z")" );
		{
			ScopedEnforcement enforcement{ "calendars" };
			let user = User( "calOccurrencesUser" );
			if( !enforcement.WasEnforced )
				EXPECT_TRUE( RunQL(week, {}, user).is_array() );
			enforcement.Enforce();
			EXPECT_EQ( Status([&]{ RunQL(week, {}, user); }), EHttpStatus::Forbidden );
			EXPECT_EQ( Status([&]{ RunQL(week, {}, Jde::UserPK{}); }), EHttpStatus::Unauthorized );
		}
		RunQL( Ƒ("deleteCalendar( id:{} )", calendarId) );
		EXPECT_EQ( Refused([&]{ RunQL(query(R"(calendar:"calOccurrencesRefused", start:"2026-07-06T00:00:00Z", end:"2026-07-07T00:00:00Z")")); }).first, EHttpStatus::NotFound ) << "a deleted calendar is found no more";
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//copyCalendarPattern copies a pattern and its shifts to a new date, the date read as a create reads one, and answers the copy;
	//it takes the date's refusals as a create does, and a refused copy writes nothing.
	TEST_F( CalendarTests, CopyLandsWhole ){
		let calendarId = Fresh( "calCopy" );
		let patternId = FirstPattern( calendarId );
		Shift( patternId, "Day", Weekdays, 0, 480, R"([{"start":240,"minutes":30}])" );
		Shift( patternId, "Night", Weekdays, 960, 480 );
		let copy = RunQL( Ƒ(R"(copyCalendarPattern( id:{}, effective:"2026-08-03T15:00:00" ){{ id calendarId effective cycleDays }})", patternId) );
		let copyId = Id( copy );
		EXPECT_NE( copyId, patternId );
		EXPECT_EQ( Json::AsNumber<uint>(copy.as_object(), "calendarId"), calendarId );
		EXPECT_TP( Time(copy, "effective"), Utc(2026,8,3,10) ) << "15:00 local Monday, moved back to the production day's start, 06:00 EDT";
		EXPECT_EQ( Json::AsNumber<uint>(copy.as_object(), "cycleDays"), 7u );
		EXPECT_FALSE( copy.as_object().contains("cycleStart") ) << "only the columns asked for:  " << serialize( copy );
		let patterns = Patterns( calendarId );
		ASSERT_EQ( patterns.size(), 2u );
		EXPECT_TP( Time(patterns.back(), "effective"), Utc(2026,8,3,10) );
		let strip = []( jarray rows ){ for( auto& r : rows ) r.as_object().erase( "id" ); return rows; };
		EXPECT_EQ( Shifts(copyId).size(), 2u );
		EXPECT_EQ( strip(Shifts(copyId)), strip(Shifts(patternId)) ) << "name, days, start, minutes and breaks, in order";

		let variables = RunQL( "copyCalendarPattern( id:$id, effective:$effective ){ id effective }", {{"id", copyId}, {"effective", "2026-08-10T06:00:00"}} );
		EXPECT_TP( Time(variables, "effective"), Utc(2026,8,10,10) ) << "through variables, and a copy of a copy";
		EXPECT_EQ( Shifts(Id(variables)).size(), 2u );

		EXPECT_REFUSED( RunQL(Ƒ(R"(copyCalendarPattern( id:{}, effective:"2026-08-03T06:00:00" ){{ id }})", patternId)), "already has a pattern effective 2026-08-03" );
		EXPECT_REFUSED( RunQL(Ƒ("copyCalendarPattern( id:{} ){{ id }}", patternId)), "effective is required" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(copyCalendarPattern( id:{}, effective:"someday" ){{ id }})", patternId)), "effective is not an ISO time" );
		EXPECT_REFUSED( RunQL(R"(copyCalendarPattern( effective:"2026-08-17T06:00:00" ){ id })"), "needs the row's id" );
		EXPECT_EQ( Refused([&]{ RunQL(R"(copyCalendarPattern( id:999999, effective:"2026-08-17T06:00:00" ){ id })"); }).first, EHttpStatus::NotFound );
		EXPECT_EQ( Patterns(calendarId).size(), 3u ) << "a refused copy writes nothing";
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//A copy whose shift is refused leaves no pattern:  Monday's night shift runs 420 minutes into Tuesday, the copy takes effect
	//that Tuesday, and its Tuesday shift starts first thing - refused at the seam, and the pattern made for it purged.
	TEST_F( CalendarTests, CopyWhoseShiftFailsLeavesNoPattern ){
		let calendarId = Fresh( "calCopyFails" );
		Shift( FirstPattern(calendarId), "Night", 1, 1380, 480 );
		let sourceId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-08T06:00:00" ){{ id }})", calendarId)) );//a Wednesday:  the night before it is Tuesday's, which has none.
		Shift( sourceId, "Early", 2, 0, 60 );
		EXPECT_REFUSED( RunQL(Ƒ(R"(copyCalendarPattern( id:{}, effective:"2026-07-07T06:00:00" ){{ id }})", sourceId)), "Shift 'Night' runs 420 minutes into 2026-07-07, the first day of a new pattern, past the start of its shift 'Early'" );
		let patterns = Patterns( calendarId, true );
		EXPECT_EQ( patterns.size(), 2u ) << "the copy's pattern went with its refusal:  " << serialize( patterns );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//The copy's rights are the caller's Create on both tables, asked before the first write.  calendarShifts enforced, patterns not.
	TEST_F( CalendarTests, CopyRightsAreAskedFirst ){
		let calendarId = Fresh( "calCopyRights" );
		let patternId = FirstPattern( calendarId );
		Shift( patternId, "Day", Weekdays, 0, 480 );
		{
			ScopedEnforcement enforcement{ "calendarShifts" };
			enforcement.Enforce();
			let user = User( "calCopyRightsUser" );
			EXPECT_EQ( Status([&]{ RunQL(Ƒ(R"(copyCalendarPattern( id:{}, effective:"2026-08-03T06:00:00" ){{ id }})", patternId), {}, user); }), EHttpStatus::Forbidden );
			EXPECT_EQ( Status([&]{ RunQL(Ƒ("copyCalendarPattern( id:{} ){{ id }}", patternId), {}, user); }), EHttpStatus::Forbidden ) << "before the message that effective is missing";
			EXPECT_EQ( Patterns(calendarId).size(), 1u ) << "no shift-less copy";
		}
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//A rotation's anchor is copied as the instant it is:  January's 06:00 EST stays that into a copy taking effect under EDT.  A
	//deleted source copies too, as a live pattern with its shifts.
	TEST_F( CalendarTests, CopyKeepsTheAnchorAndTakesADeletedSource ){
		let calendarId = Fresh( "calCopyAnchor" );
		let sourceId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00", cycleDays:14, cycleStart:"2026-01-05T06:00:00" ){{ id }})", calendarId)) );
		Shift( sourceId, "Day", Weekdays, 0, 480 );
		let copy = RunQL( Ƒ(R"(copyCalendarPattern( id:{}, effective:"2026-08-03T06:00:00" ){{ id cycleDays cycleStart }})", sourceId) );
		EXPECT_EQ( Json::AsNumber<uint>(copy.as_object(), "cycleDays"), 14u );
		EXPECT_TP( Time(copy, "cycleStart"), Utc(2026,1,5,11) ) << "as answered";
		let stored = RunQL( Ƒ("calendarPatterns( id:{} ){{ cycleStart }}", Id(copy)) ).as_array();
		ASSERT_EQ( stored.size(), 1u );
		EXPECT_TP( Time(stored[0], "cycleStart"), Utc(2026,1,5,11) ) << "as stored";

		RunQL( Ƒ("deleteCalendarPattern( id:{} )", sourceId) );
		let revived = Id( RunQL(Ƒ(R"(copyCalendarPattern( id:{}, effective:"2026-09-07T06:00:00" ){{ id }})", sourceId)) );
		EXPECT_EQ( Shifts(revived).size(), 1u );
		let live = Patterns( calendarId );
		let isLive = [&]( uint id ){ return std::ranges::any_of( live, [id]( const jvalue& p ){ return Id(p)==id; } ); };
		EXPECT_TRUE( isLive(revived) ) << serialize( live );
		EXPECT_FALSE( isLive(sourceId) ) << "the source stays deleted:  " << serialize( live );
		RunQL( Ƒ("purgeCalendar( id:{} )", calendarId) );
	}

	//status{} carries the server's zone, a new calendar's default (spec *Open questions* #8):  the process's, a name the zone
	//database knows, or UTC.
	TEST_F( CalendarTests, StatusCarriesTheZone ){
		let status = RunQL( "status{ timeZone }" ).as_object();
		let zone = Json::AsString( status, "timeZone" );
		string expected{ "UTC" };
		try{ expected = std::chrono::current_zone()->name(); }catch( const std::exception& ){}
		EXPECT_EQ( zone, expected );
		EXPECT_NO_THROW( Calendar::ValidateZone(zone) );
		EXPECT_EQ( Json::AsString(IApp::Status(), "timeZone"), zone ) << "the one the connections' statuses carry too (ConnectionQLAwait)";
	}
}
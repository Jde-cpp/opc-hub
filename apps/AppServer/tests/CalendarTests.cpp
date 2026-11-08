#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/LocalSubscriptions.h>
#include "helpers.h"
#define let const auto

//The calendar tables (config/app-meta.jsonnet) and CalendarMAwait (libs/calendar/src/server), through the AppServer's own QL as
//the web's forms and /graphql reach them:  the tables sync, the side effects land, a zone-less time is read in the calendar's
//zone, and each refusal of docs/calendar/spec.md *Validation* that needs the database comes back BadRequest with the library's
//message.  America/New_York:  EDT is UTC-4 in July, so a 06:00 day start is 10:00Z.
namespace Jde::App::Server::Tests{
	using namespace std::chrono;
	constexpr sv NewYork{ "America/New_York" };
	constexpr uint32 Weekdays{ 31 };

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

		let calendarId = Fresh( "calRefusals" );
		let patternId = FirstPattern( calendarId );
		ASSERT_NE( patternId, 0u );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendar( id:{}, timeZone:"Nowhere" ))", calendarId)), "timeZone 'Nowhere'" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendar( id:{}, dayStart:1500 ))", calendarId)), "dayStart 1500" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T00:00:00", cycleDays:14 ){{ id }})", calendarId)), "A 14-day cycle needs a cycleStart" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T00:00:00", cycleDays:0 ){{ id }})", calendarId)), "cycleDays 0 is not 1 to 31" );
		EXPECT_REFUSED( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{} ){{ id }})", calendarId)), "already has a first pattern" );//a second null effective, which the unique index would let through.
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
		EXPECT_REFUSED( RunQL(Ƒ(R"(updateCalendarShift( id:{}, minutes:0 ))", day)), "runs 0 minutes" );
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
		RunQL( Ƒ(R"(updateCalendarPattern( id:{}, effective:"2026-07-13T02:00:00" ))", Id(created)) );//02:00 local Monday is still Sunday's production day.
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

	//The first pattern can't be duplicated by a restore either:  a deleted first pattern, another made, the restore refused.
	TEST_F( CalendarTests, RestoreOfASecondFirstPatternIsRefused ){
		let calendarId = Fresh( "calRestoreFirst" );
		let firstId = FirstPattern( calendarId );
		let secondId = Id( RunQL(Ƒ(R"(createCalendarPattern( calendarId:{}, effective:"2026-07-06T06:00:00" ){{ id }})", calendarId)) );
		RunQL( Ƒ("deleteCalendarPattern( id:{} )", firstId) );
		RunQL( Ƒ("updateCalendarPattern( id:{}, effective:null )", secondId) );
		EXPECT_TRUE( Patterns(calendarId).front().as_object().at("effective").is_null() );
		EXPECT_REFUSED( RunQL(Ƒ("restoreCalendarPattern( id:{} )", firstId)), "already has a first pattern" );
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
}
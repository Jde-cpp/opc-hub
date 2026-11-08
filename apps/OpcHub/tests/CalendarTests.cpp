#include <jde/ql/ql.h>
#include <jde/app/IApp.h>
#include "../../OpcGateway/src/GatewayAppClient.h"
#include "helpers.h"
#define let const auto

//The calendar tables' validation and the calendar's own operations in the hub (docs/calendar/spec.md *Validation*, *Reads*):
//HubQL's ladders call App::Server::CustomMutation and CustomQuery, and App::Server::Configure registers the system names, so
//CalendarMAwait and OccurrencesQLAwait answer through the in-process QL as the AppServer's own does.  The rules themselves are
//the AppServer suite's (apps/AppServer/tests/CalendarTests.cpp).
namespace Jde::Opc::Hub::Tests{
	TEST( CalendarTests, HubRefusesAsTheAppServerDoes ){
		try{
			Gateway::AppClient()->QuerySync<jobject>( R"(createCalendar( name:"hubBadZone", slug:"hubBadZone", timeZone:"Mars/Olympus_Mons" ){ id })", {} );
			FAIL() << "a zone the server doesn't know was accepted";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest );
			EXPECT_NE( string{e.what()}.find("Mars/Olympus_Mons"), string::npos ) << e.what();
		}
		EXPECT_TRUE( Gateway::AppClient()->QuerySync<jarray>(R"(calendars( slug:"hubBadZone" ){ id })", {}).empty() ) << "nothing was written";
	}

	//calendarOccurrences and copyCalendarPattern are the hub's too, and status{} carries the zone.
	TEST( CalendarTests, HubAnswersTheCalendarsOwnOperations ){
		auto client = Gateway::AppClient();
		for( let& v : client->QuerySync<jarray>(R"(calendars( slug:"hubCalOps" ){ id })", {}) )
			client->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", QL::AsId<uint>(v)), {} );
		let calendarId = QL::AsId<uint>( client->QuerySync<jobject>(R"(createCalendar( name:"hubCalOps", slug:"hubCalOps", timeZone:"America/New_York", dayStart:360 ){ id })", {}) );
		let patternId = QL::AsId<uint>( client->QuerySync<jarray>(Ƒ("calendarPatterns( calendarId:{} ){{ id }}", calendarId), {}).at(0) );
		client->QuerySync<jobject>( Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"Day", days:31, start:0, minutes:480 ){{ id }})", patternId), {} );

		let occurrences = client->QuerySync<jarray>( R"(calendarOccurrences( calendar:"hubCalOps", start:"2026-07-06T00:00:00", end:"2026-07-13T00:00:00" ){ name day })", {} );
		EXPECT_EQ( occurrences.size(), 5u ) << serialize( occurrences );
		let copy = client->QuerySync<jobject>( Ƒ(R"(copyCalendarPattern( id:{}, effective:"2026-08-03T06:00:00" ){{ id }})", patternId), {} );
		let copyId = QL::AsId<uint>( copy );
		EXPECT_NE( copyId, patternId );
		EXPECT_EQ( client->QuerySync<jarray>(Ƒ("calendarShifts( calendarPatternId:{} ){{ id }}", copyId), {}).size(), 1u );
		EXPECT_EQ( Json::AsString(client->QuerySync<jobject>("status{ timeZone }", {}), "timeZone"), App::IApp::TimeZoneName() );
		client->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", calendarId), {} );
	}
}
#include <jde/app/IApp.h>
#include "../../OpcGateway/src/GatewayAppClient.h"
#include "helpers.h"
#define let const auto

//The calendar tables' validation in the hub (docs/calendar/spec.md *Validation*):  HubQL's mutation ladder calls
//App::Server::CustomMutation, so CalendarMAwait refuses through the in-process QL as the AppServer's own does.  The rules
//themselves are the AppServer suite's (apps/AppServer/tests/CalendarTests.cpp).
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
}
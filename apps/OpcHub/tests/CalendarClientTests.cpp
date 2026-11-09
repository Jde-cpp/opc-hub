#include <jde/ql/ql.h>
#include <jde/calendar/client/Calendars.h>
#include <jde/tests/WaitUntil.h>
#include "../../OpcGateway/src/GatewayAppClient.h"
#define let const auto

//The gateway role's cache of the shift calendars (docs/calendar/spec.md *The client*) in the hub, where the app client is
//in-process:  its reads are System's on the HubQL, and its subscriptions sit in the LocalSubscriptions map the stock mutation
//publishes to.  The socket variant, and the reconnect the hub never makes, are the gateway suite's
//(apps/OpcGateway/tests/CalendarClientTests.cpp).
namespace Jde::Opc::Hub::Tests{
	namespace Calendars = Calendar::Client::Calendars;

	TEST( CalendarClientTests, HubHoldsACalendarAndFollowsAnEdit ){
		auto client = Gateway::AppClient();
		for( let& v : client->QuerySync<jarray>(R"(calendars( slug:"hubCalClient" ){ id })", {}) )
			client->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", QL::AsId<uint32>(v)), {} );
		let id = QL::AsId<uint32>( client->QuerySync<jobject>(R"(createCalendar( name:"hubCalClient", slug:"hubCalClient", timeZone:"America/New_York", dayStart:360 ){ id })", {}) );
		sp<const Calendar::Schedule> created;
		ASSERT_TRUE( WaitUntil([&]{ created = Calendars::Find( id ); return created && created->Patterns().size()==1; }) ) << "created through the hub's QL, and not in the gateway role's cache";
		EXPECT_EQ( Calendars::Find("hubCalClient"), created );

		let shiftId = QL::AsId<uint32>( client->QuerySync<jobject>(Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"Day", days:31, start:0, minutes:480 ){{ id }})", created->Patterns().front().Id), {}) );
		auto shiftName = [id]()->string{
			let calendar = Calendars::Find( id );
			return calendar && calendar->Patterns().size() && calendar->Patterns().front().Shifts.size() ? calendar->Patterns().front().Shifts.front().Name : string{};
		};
		ASSERT_TRUE( WaitUntil([&]{ return shiftName()=="Day"; }) );
		client->QuerySync<jvalue>( Ƒ(R"(updateCalendarShift( id:{}, name:"Days" ))", shiftId), {} );
		EXPECT_TRUE( WaitUntil([&]{ return shiftName()=="Days"; }) ) << "an edit, followed";

		client->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", id), {} );
		EXPECT_TRUE( WaitUntil([&]{ return !Calendars::Find(id); }) );
	}
}
#include <thread>
#include <boost/asio/ip/tcp.hpp>
#include <jde/fwk/settings.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/app/client/appClient.h>
#include <jde/calendar/client/Calendars.h>
#include "../src/GatewayAppClient.h"
#include "utils/helpers.h"
#include "../../AppServer/src/appStartup.h"
#include "../../AppServer/src/ql/AppServerQL.h"

#define let const auto

//The gateway's cache of the shift calendars (docs/calendar/spec.md *The client*, libs/calendar/src/client) against the
//embedded AppServer, over the gateway's own app socket as a split gateway's:  Gateway::Connect subscribed and loaded it, an
//edit reaches it within the debounce, and a reconnect brings back the edits made while the socket was down.
namespace Jde::Opc::Gateway::Tests{
	namespace Calendars = Calendar::Client::Calendars;
	using namespace std::chrono_literals;

	struct CalendarClientTests : ::testing::Test{
		//A calendar of `slug`, created anew through the gateway's client:  one an earlier run left - the mysql run keeps its db - is purged first.
		Ω Fresh( sv slug )ε->uint32{
			for( let& v : AppClient()->QuerySync<jarray>(Ƒ(R"(calendars( slug:"{}" ){{ id }})", slug), {}) )
				Purge( QL::AsId<uint32>(v) );
			return QL::AsId<uint32>( AppClient()->QuerySync<jobject>(Ƒ(R"(createCalendar( name:"{0}", slug:"{0}", timeZone:"America/New_York", dayStart:360 ){{ id }})", slug), {}) );
		}
		Ω Purge( uint32 id )ε->void{ AppClient()->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", id), {} ); }
		//On the embedded AppServer's own QL, as System:  not the gateway's socket, and not its user.
		Ω OnServer( string query )ε->jvalue{
			return BlockAwait<QL::QLAwait<jvalue>,jvalue>( QL::QLAwait<jvalue>{move(query), {}, Jde::UserPK{Jde::UserPK::System}, App::Server::QLPtr()} );
		}
		Ω Embedded()ι->bool{ return Settings::FindBool( "/testing/embeddedAppServer" ).value_or( true ); }
		Ω Shift( uint32 patternId, sv name )ε->uint32{
			return QL::AsId<uint32>( AppClient()->QuerySync<jobject>(Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"{}", days:31, start:0, minutes:480 ){{ id }})", patternId, name), {}) );
		}
		//The calendar's first shift's name as the cache holds it;  empty with no calendar, pattern or shift.
		Ω ShiftName( uint32 calendarId )ι->string{
			let calendar = Calendars::Find( calendarId );
			return calendar && calendar->Patterns().size() && calendar->Patterns().front().Shifts.size() ? calendar->Patterns().front().Shifts.front().Name : string{};
		}
		//The cache holds the calendar with the pattern createCalendar made:  the two creates' events are one reload, or two.
		Ω Held( uint32 calendarId )ι->sp<const Calendar::Schedule>{
			sp<const Calendar::Schedule> y;
			WaitUntil( [&]{ y = Calendars::Find( calendarId ); return y && y->Patterns().size()==1; } );
			return y && y->Patterns().size()==1 ? y : nullptr;
		}
		//The load count once it has stood still past a debounce and a read:  no reload is waiting or running.
		Ω Settled()ι->uint{
			for( auto count = Calendars::LoadCount();; ){
				std::this_thread::sleep_for( 750ms );
				let now = Calendars::LoadCount();
				if( now==count )
					return count;
				count = now;
			}
		}
	};

	//What Listen's function heard, shared with it:  a reload in flight may call it once more after StopListen.
	struct HeardCalendars final{
		α Contains( uint32 id )ι->bool{ lg _{ Mutex }; return Ids.contains( id ); }
		α Clear()ι->void{ lg _{ Mutex }; Ids.clear(); }
		std::mutex Mutex;
		flat_set<uint32> Ids;
	};
	Ω listen( sp<HeardCalendars> heard )ι->uint{
		return Calendars::Listen( [heard]( const flat_set<uint32>& changed ){ lg _{ heard->Mutex }; heard->Ids.insert( changed.begin(), changed.end() ); } );
	}

	//A calendar created through the gateway's own client is in the cache and Listen names it;  a shift's update is followed, and
	//names its calendar alone;  a delete removes it.
	TEST_F( CalendarClientTests, FollowsCreateUpdateAndDelete ){
		auto heard = ms<HeardCalendars>();
		let handle = listen( heard );
		absl::Cleanup stop = [handle]{ Calendars::StopListen( handle ); };

		let id = Fresh( "calClient" );
		let created = Held( id );
		ASSERT_TRUE( created ) << "created through the gateway's client, and not in its cache";
		EXPECT_EQ( created->Slug(), "calClient" );
		EXPECT_EQ( created->Zone().name(), "America/New_York" );
		EXPECT_EQ( created->DayStartMinutes(), 360min );
		EXPECT_EQ( Calendars::Find("calClient"), created ) << "by slug, the same snapshot";
		EXPECT_TRUE( heard->Contains(id) );

		let otherId = Fresh( "calClientOther" );
		let other = Held( otherId );
		ASSERT_TRUE( other );
		let shiftId = Shift( created->Patterns().front().Id, "Day" );
		ASSERT_TRUE( WaitUntil([&]{ return ShiftName(id)=="Day"; }) );
		Settled();
		heard->Clear();

		AppClient()->QuerySync<jvalue>( Ƒ(R"(updateCalendarShift( id:{}, name:"Days" ))", shiftId), {} );
		ASSERT_TRUE( WaitUntil([&]{ return ShiftName(id)=="Days"; }) ) << "a shift's update, followed";
		EXPECT_TRUE( heard->Contains(id) );
		EXPECT_FALSE( heard->Contains(otherId) ) << "its rows were the last load's";
		EXPECT_EQ( Calendars::Find(otherId), other ) << "an unchanged calendar keeps its snapshot";
		EXPECT_EQ( created->Patterns().front().Shifts.size(), 0u ) << "a snapshot held is the rows it was built from";

		heard->Clear();
		AppClient()->QuerySync<jvalue>( Ƒ("deleteCalendar( id:{} )", id), {} );
		ASSERT_TRUE( WaitUntil([&]{ return !Calendars::Find(id); }) ) << "a soft-deleted calendar leaves the cache";
		EXPECT_FALSE( Calendars::Find("calClient") );
		EXPECT_TRUE( heard->Contains(id) );
		EXPECT_TRUE( Calendars::Find(otherId) );

		Purge( id );
		Purge( otherId );
		EXPECT_TRUE( WaitUntil([&]{ return !Calendars::Find(otherId); }) );
	}

	//The calendars resource enforced with no grant for the gateway's user (spec *Authorization*):  the reload an edit raises is
	//refused, so the cache holds nothing;  opened again, as it ships, the next reload fills it.
	TEST_F( CalendarClientTests, ARefusedReadHoldsNothing ){
		if( !Embedded() )
			GTEST_SKIP() << "the resource is enforced on the embedded AppServer's own QL.";
		let id = Fresh( "calRefused" );
		ASSERT_TRUE( Held(id) );
		let resource = OnServer( Ƒ(R"(resources( schemaName:"{}", slug:"calendars", criteria:null ){{ id deleted }})", App::Server::AppSchema()->Name) ).as_array().at( 0 ).as_object();
		if( resource.at("deleted").is_null() )
			GTEST_SKIP() << "calendars is enforced already, so the gateway's user may hold a grant.";
		let resourceId = QL::AsId<uint32>( resource );
		absl::Cleanup open = [resourceId]{ try{ OnServer( Ƒ("deleteResource( id:{} )", resourceId) ); }catch( const std::exception& e ){ ADD_FAILURE() << "calendars wasn't opened again:  " << e.what(); } };
		OnServer( Ƒ("restoreResource( id:{} )", resourceId) );
		OnServer( Ƒ(R"(updateCalendar( id:{}, name:"calRefused2" ))", id) );
		EXPECT_TRUE( WaitUntil([&]{ return !Calendars::Find(id); }) ) << "the read was refused, and the calendar is still held";

		std::move( open ).Invoke();
		OnServer( Ƒ(R"(updateCalendar( id:{}, name:"calRefused3" ))", id) );
		EXPECT_TRUE( WaitUntil([&]{ let held = Calendars::Find( id ); return held && held->Name()=="calRefused3"; }) ) << "opened again, the next reload holds it";
		Purge( id );
	}

	Ω closedPort()ε->PortType{
		boost::asio::io_context ctx;
		boost::asio::ip::tcp::acceptor listener{ ctx, {boost::asio::ip::make_address("127.0.0.1"), 0} };
		return listener.local_endpoint().port();
	}//closed again on return, so a connect to it is refused.

	//The app socket dropped and reconnected:  the session's close reconnects at once, so the gap is opened by pointing the
	//first attempts at a closed port.  An edit made on the AppServer meanwhile raises no event here, and the reload on the new
	//session, IAppClient::OnReconnected's, is what brings it;  the subscriptions, re-issued, follow the next edit.
	TEST_F( CalendarClientTests, ReconnectBringsTheEditsMadeWhileAway ){
		if( !Embedded() )
			GTEST_SKIP() << "the edit while away is made on the embedded AppServer's own QL.";
		let id = Fresh( "calReconnect" );
		let created = Held( id );
		ASSERT_TRUE( created );
		let shiftId = Shift( created->Patterns().front().Id, "Day" );
		ASSERT_TRUE( WaitUntil([&]{ return ShiftName(id)=="Day"; }) );
		let loads = Settled();

		let port = App::Client::ServerSettings::Port();
		let wait = Settings::FindString( "/server/reconnectWait" ).value_or( "PT5S" );
		absl::Cleanup restore = [port, wait]{ try{ Settings::Set("/server/port", port); Settings::Set("/server/reconnectWait", jvalue{sv{wait}}); }catch( const std::exception& ){} };//every later connect reads them.
		Settings::Set( "/server/reconnectWait", "PT1S" );
		Settings::Set( "/server/port", closedPort() );
		absl::Cleanup forget = []{ ForgetAppSession(); };//the suites' shared socket is bound to the session about to go.
		AppClient()->CloseSocketSession( false, SRCE_CUR );
		ASSERT_TRUE( WaitUntil([]{ return !AppClient()->Connected(); }) );

		OnServer( Ƒ(R"(updateCalendarShift( id:{}, name:"Away" ))", shiftId) );
		std::this_thread::sleep_for( 750ms );//past a debounce:  an event would have reloaded by now.
		EXPECT_FALSE( AppClient()->Connected() );
		EXPECT_EQ( ShiftName(id), "Day" ) << "no event reaches a gateway with no socket";
		EXPECT_EQ( Calendars::LoadCount(), loads );

		Settings::Set( "/server/port", port );
		ASSERT_TRUE( WaitUntil([]{ return AppClient()->Connected(); }, 30s) ) << "the gateway's app client never reconnected";
		ASSERT_TRUE( WaitUntil([&]{ return ShiftName(id)=="Away"; }) ) << "the edit made while away, read on the new session";
		EXPECT_EQ( Settled(), loads+1 ) << "one reload, the reconnect's";

		AppClient()->QuerySync<jvalue>( Ƒ(R"(updateCalendarShift( id:{}, name:"Back" ))", shiftId), {} );
		EXPECT_TRUE( WaitUntil([&]{ return ShiftName(id)=="Back"; }) ) << "the subscriptions, re-issued on the new session, follow an edit";
		Purge( id );
	}
}
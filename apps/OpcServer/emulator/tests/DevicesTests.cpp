//ParseDevices:  what /emulator/devices turns into, built with no session and no PLC server (T2).
#include "../Devices.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	Ω devices( sv json, const FindField& findField={} )ε->vector<Device>{ return ParseDevices( boost::json::parse(string{json}).as_array(), findField ); }
	constexpr sv twoPumps{ R"([
		{ "path":"pump1", "tags":[ { "name":"motorRpm", "mode":"follow" }, { "name":"status", "mode":"command" } ] },
		{ "path":"site/2~pump2", "tags":[ { "name":"motorRpm", "mode":"sine" } ] }
	])" };

	TEST( ParseDevicesTests, NamesAreTheLastPathSegment ){
		let y = devices( twoPumps );
		ASSERT_EQ( y.size(), 2u );
		EXPECT_EQ( y[0].Path, "pump1" ); EXPECT_EQ( y[0].Name, "pump1" );
		EXPECT_EQ( y[1].Path, "site/2~pump2" ); EXPECT_EQ( y[1].Name, "pump2" );//past the '/' and the ns separator
	}
	TEST( ParseDevicesTests, CommandTagsAreSubscribedNotGenerated ){
		let y = devices( twoPumps );
		ASSERT_EQ( y[0].Tags.size(), 2u );
		EXPECT_TRUE( y[0].Tags[0].Generator );
		EXPECT_FALSE( y[0].Tags[1].Generator );
		EXPECT_FALSE( y[0].Tags[1].Field );
		EXPECT_TRUE( y[0].Command );//a device starts commanded on until the subscription says otherwise
	}
	TEST( ParseDevicesTests, PublishedFieldsMapThroughTheContract ){
		let contract = []( sv name )ι->optional<uint>{ return name=="pump1.motorRpm" ? optional<uint>{3} : optional<uint>{}; };
		let y = devices( twoPumps, contract );
		ASSERT_TRUE( y[0].Tags[0].Field ); EXPECT_EQ( *y[0].Tags[0].Field, 3u );//"<device>.<tag>" is the contract's field name
		EXPECT_FALSE( y[0].Tags[1].Field );//command tags are never published
		EXPECT_FALSE( y[1].Tags[0].Field );//not in the contract: written over the session
	}
	TEST( ParseDevicesTests, NothingIsPublishedOnTheWriteTransport ){
		for( let& device : devices(twoPumps) ){
			for( let& tag : device.Tags )
				EXPECT_FALSE( tag.Field ) << device.Name << '.' << tag.Spec.Name;
		}
	}
	TEST( ParseDevicesTests, OwnerIsSetAndSelfIsLeftToTheEmulator ){
		let y = devices( twoPumps );
		for( let& device : y ){
			for( let& tag : device.Tags ){
				EXPECT_EQ( tag.Owner, &device );
				EXPECT_EQ( tag.Self, nullptr );
			}
		}
	}
	TEST( ParseDevicesTests, AGeneratedTagCarriesItsQualitySchedule ){
		auto y = devices( R"([{ "path":"pump1", "tags":[ { "name":"motorRpm", "mode":"sine", "quality":[{ "status":"BadSensorFailure", "duration":"PT5S" }] }, { "name":"status", "mode":"toggle" } ] }])" );
		auto& tag = y[0].Tags[0];
		EXPECT_EQ( tag.Status, UA_STATUSCODE_GOOD );//until the first cycle says otherwise
		tag.Status = tag.Quality.Apply( 1s, 1000, tag.Value );
		EXPECT_EQ( tag.Status, UA_STATUSCODE_BADSENSORFAILURE );
		EXPECT_EQ( y[0].Tags[1].Quality.Apply(1s, 1, y[0].Tags[1].Value), UA_STATUSCODE_GOOD );//no schedule: Good
	}
	TEST( ParseDevicesTests, Refusals ){
		EXPECT_THROW( devices("[]"), Exception );
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[] }])"), Exception );
		EXPECT_THROW( devices(R"([{ "tags":[ { "name":"motorRpm" } ] }])"), Exception );//no path
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"motorRpm", "mode":"randomWalk", "step":0 } ] }])"), Exception );//#9 propagates
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"status", "mode":"command", "quality":[{ "status":"Bad", "duration":"PT1S" }] } ] }])"), Exception );//so does a quality nothing would write
	}
}

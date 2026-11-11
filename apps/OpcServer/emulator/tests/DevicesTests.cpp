//ParseDevices:  what /emulator/devices turns into, built with no session and no PLC server (T2), and Device::Sample, a period of it.
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
	TEST( ParseDevicesTests, AProductionTagIsFollowedByItsRejects ){
		let contract = []( sv name )ι->optional<uint>{ return name=="pump1.totalCount" ? optional<uint>{5} : name=="pump1.rejectCount" ? optional<uint>{6} : optional<uint>{}; };
		let y = devices( R"([
			{ "path":"pump1", "tags":[ { "name":"totalCount", "mode":"production", "reject":0.05, "quality":[{ "status":"Bad", "duration":"PT1S" }] }, { "name":"status", "mode":"command" } ] },
			{ "path":"pump2", "tags":[ { "name":"units", "mode":"production", "reject":0.05, "rejectTag":"scrap" }, { "name":"plain", "mode":"production" } ] }
		])", contract );
		ASSERT_EQ( y[0].Tags.size(), 3u );
		EXPECT_EQ( y[0].Tags[0].Spec.Name, "totalCount" ); EXPECT_EQ( y[0].Tags[1].Spec.Name, "rejectCount" ); EXPECT_EQ( y[0].Tags[2].Spec.Name, "status" );
		EXPECT_EQ( y[0].Tags[0].Field, optional<uint>{5} ); EXPECT_EQ( y[0].Tags[1].Field, optional<uint>{6} );
		EXPECT_EQ( y[0].Tags[0].Type, &UA_TYPES[UA_TYPES_UINT32] ); EXPECT_EQ( y[0].Tags[1].Type, &UA_TYPES[UA_TYPES_UINT32] );//what a session write goes as
		EXPECT_EQ( y[0].Tags[0].Spec.Quality.size(), 1u ); EXPECT_TRUE( y[0].Tags[1].Spec.Quality.empty() );//the schedule is the count's
		EXPECT_EQ( y[0].StatusTag, optional<uint>{2} );
		ASSERT_EQ( y[1].Tags.size(), 3u );
		EXPECT_EQ( y[1].Tags[1].Spec.Name, "scrap" ); EXPECT_EQ( y[1].Tags[2].Spec.Name, "plain" );//no share, no second tag
		EXPECT_FALSE( y[1].StatusTag );
	}
	TEST( DeviceTests, NothingAdvancesWhileTheCommandIsOff ){
		auto y = devices( R"([{ "path":"pump1", "tags":[ { "name":"totalCount", "mode":"production", "reject":1 }, { "name":"status", "mode":"command" } ] }])" );
		auto& pump = y[0];
		pump.Sample( 3s );
		EXPECT_TRUE( pump.On ); EXPECT_EQ( pump.Tags[0].Value, 3 ); EXPECT_EQ( pump.Tags[1].Value, 3 );
		pump.Command = false;//the UI's write
		pump.Sample( 1h );
		EXPECT_FALSE( pump.On ); EXPECT_EQ( pump.Tags[0].Value, 3 ); EXPECT_EQ( pump.Tags[1].Value, 3 );
		pump.Command = true;
		pump.Sample( 2s );
		EXPECT_EQ( pump.Tags[0].Value, 5 );
	}
	TEST( DeviceTests, ACountFollowsAGeneratedStatus ){
		auto y = devices( R"([{ "path":"pump2", "tags":[ { "name":"totalCount", "mode":"production" }, { "name":"status", "mode":"toggle", "period":"PT3S" } ] }])" );
		auto& pump = y[0];
		for( uint second=1; second<=12; ++second ){//on [0,3), off [3,6), on [6,9), off [9,12);  listed after the count, sampled before it
			pump.Sample( 1s );
			let on = second%6<3;
			EXPECT_EQ( pump.On, on ) << second;
			EXPECT_EQ( pump.Tags[1].Value, on ? 1 : 0 ) << second;
			EXPECT_EQ( pump.Tags[0].Value, second<3 ? second : second<6 ? 2 : second<9 ? second-3 : second<12 ? 5 : 6 ) << second;
		}
	}
	TEST( DeviceTests, AScheduledStopHoldsStatusOffAndSetsItsCode ){
		let contract = []( sv name )ι->optional<uint>{ return name=="pump2.reasonCode" ? optional<uint>{9} : optional<uint>{}; };
		auto y = devices( R"([{ "path":"pump2", "stops":[ { "every":"PT10S", "for":"PT3S", "code":7 }, { "every":"PT20S", "for":"PT5S", "code":8 } ],
			"tags":[ { "name":"totalCount", "mode":"production" }, { "name":"status", "mode":"toggle", "period":"PT1H" } ] }])", contract );
		auto& pump = y[0];
		ASSERT_EQ( pump.Tags.size(), 3u );
		ASSERT_EQ( pump.ReasonTag, optional<uint>{2} );
		let& count = pump.Tags[0]; let& status = pump.Tags[1]; let& reason = pump.Tags[2];
		EXPECT_EQ( reason.Spec.Name, "reasonCode" ); EXPECT_EQ( reason.Field, optional<uint>{9} ); EXPECT_EQ( reason.Type, &UA_TYPES[UA_TYPES_INT32] );
		uint units{};
		for( uint second=1; second<=45; ++second ){//stopped [10,13) [20,25) [30,33) [40,45):  the first active stop gives the code
			pump.Sample( 1s );
			let code = second%10>=3 || second<10 ? ( second>=20 && second%20<5 ? 8 : 0 ) : 7;
			if( !code )
				++units;
			EXPECT_EQ( pump.Code, code ) << second;
			EXPECT_EQ( pump.On, !code ) << second;
			EXPECT_EQ( status.Value, code ? 0 : 1 ) << second;
			EXPECT_EQ( reason.Value, code ) << second;
			EXPECT_EQ( count.Value, units ) << second;
		}
		EXPECT_EQ( units, 45u-3-5-3-5 );
	}
	TEST( ParseDevicesTests, StopRefusals ){
		constexpr sv tags{ R"("tags":[ { "name":"status", "mode":"toggle" } ])" };
		let stops = [&]( sv list, sv tags_ ){ return devices( Ƒ(R"([{{ "path":"pump2", "stops":{}, {} }}])", list, tags_) ); };
		EXPECT_NO_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", tags) );
		EXPECT_NO_THROW( stops("[]", R"("tags":[ { "name":"status", "mode":"command" } ])") );//no stops, nothing to hold
		EXPECT_THROW( stops(R"({ "every":"PT10S", "for":"PT3S", "code":7 })", tags), Exception );//not a list
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S" }])", tags), Exception );//no code
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":0 }])", tags), Exception );//0 is no reason
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT10S", "code":7 }])", tags), Exception );//never running
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "code":7 }])", tags), Exception );
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", R"("tags":[ { "name":"status", "mode":"command" } ])"), Exception );//the UI's status is not the emulator's to hold
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", R"("tags":[ { "name":"motorRpm", "mode":"sine" } ])"), Exception );//no status
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", R"("tags":[ { "name":"status", "mode":"toggle" }, { "name":"reasonCode" } ])"), Exception );//the stops' own tag
	}
	TEST( ParseDevicesTests, Refusals ){
		EXPECT_THROW( devices("[]"), Exception );
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[] }])"), Exception );
		EXPECT_THROW( devices(R"([{ "tags":[ { "name":"motorRpm" } ] }])"), Exception );//no path
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"motorRpm", "mode":"randomWalk", "step":0 } ] }])"), Exception );//#9 propagates
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"status", "mode":"command", "quality":[{ "status":"Bad", "duration":"PT1S" }] } ] }])"), Exception );//so does a quality nothing would write
	}
}
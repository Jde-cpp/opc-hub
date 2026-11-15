//ParseDevices:  what /emulator/devices turns into, built with no session and no PLC server (T2), and Device::Sample, a period of it.
#include "../Devices.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	constexpr TimePoint _start{ sys_days{2026y/January/1} };//the wall time of the device clock's 0 - never the wall clock's own.
	//`zone` empty:  /emulator/zone left out.
	Ω devices( sv json, const FindField& findField={}, TimePoint start=_start, sv zone={} )ε->vector<Device>{
		return ParseDevices( boost::json::parse(string{json}).as_array(), findField, zone.empty() ? nullptr : &Chrono::ToTimeZone(zone, "zone"), start );
	}
	//A machine:  a count, a `status` that stays on but for its stops, and what `rest` adds - "stops":[...], "seed":1.
	Ω machines( sv rest, TimePoint start=_start, sv zone={} )ε->vector<Device>{
		return devices( Ƒ(R"([{{ "path":"machine", {}, "tags":[ {{ "name":"speed", "mode":"follow", "follows":"totalCount", "tau":"PT1S" }}, {{ "name":"totalCount", "mode":"production" }}, {{ "name":"status", "mode":"on" }} ] }}])", rest), {}, start, zone );
	}
	using Window = std::pair<Duration,Duration>;
	//The device's first stop's windows, from the one ParseDevices placed, the clock walked from each one's end to the next's.
	Ω stopWindows( Device& device, uint count )ι->vector<Window>{
		vector<Window> y;
		for( ; y.size()<count; device.Sample(device.Stops[0].End-device.Clock) )
			y.emplace_back( device.Stops[0].Begin, device.Stops[0].End );
		return y;
	}
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
		EXPECT_FALSE( y[0].Command );//off until its first notification:  the OpcServer's `status` may read false
		EXPECT_TRUE( y[1].Command );//nothing to command it
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
		EXPECT_EQ( y[0].Tags[0].Type, &UA_TYPES[UA_TYPES_UINT32] ); EXPECT_EQ( y[0].Tags[1].Type, &UA_TYPES[UA_TYPES_UINT32] );//the mode's, until a node says otherwise
		EXPECT_EQ( y[0].Tags[2].Type, nullptr );//a command tag is never written
		EXPECT_EQ( y[0].Tags[0].Spec.Quality.size(), 1u ); EXPECT_EQ( y[0].Tags[1].Spec.Quality.size(), 1u );//one counter:  the count's faults are the rejects' too
		EXPECT_EQ( y[0].StatusTag, optional<uint>{2} );
		ASSERT_EQ( y[1].Tags.size(), 3u );
		EXPECT_EQ( y[1].Tags[1].Spec.Name, "scrap" ); EXPECT_EQ( y[1].Tags[2].Spec.Name, "plain" );//no share, no second tag
		EXPECT_FALSE( y[1].StatusTag );
	}
	TEST( DeviceTests, NothingAdvancesWhileTheCommandIsOff ){
		auto y = devices( R"([{ "path":"pump1", "tags":[ { "name":"totalCount", "mode":"production", "reject":1 }, { "name":"status", "mode":"command" } ] }])" );
		auto& pump = y[0];
		pump.Sample( 3s );
		EXPECT_FALSE( pump.On ); EXPECT_EQ( pump.Tags[0].Value, 0 );//no notification yet
		pump.Command = true;//the first
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
		//stopped [10,13) [20,25) [30,33) [40,45):  the first active stop gives the code
		let codeAt = []( uint t ){ return t%10>=3 || t<10 ? ( t>=20 && t%20<5 ? 8 : 0 ) : 7; };
		uint units{};
		for( uint second=1; second<=45; ++second ){
			pump.Sample( 1s );
			let code = codeAt( second );//as of the sample
			if( !codeAt(second-1) )//through the second before it
				++units;
			EXPECT_EQ( pump.Code, code ) << second;
			EXPECT_EQ( pump.On, !code ) << second;
			EXPECT_EQ( status.Value, code ? 0 : 1 ) << second;
			EXPECT_EQ( reason.Value, code ) << second;
			EXPECT_EQ( count.Value, units ) << second;
		}
		EXPECT_EQ( units, 45u-3-5-3-5 );
	}
	TEST( DeviceTests, APeriodIsCutAtEachStopEdge ){//a long -period, or a blocking reconnect's ten seconds in one dt
		auto y = devices( R"([{ "path":"pump2", "stops":[ { "every":"PT10S", "for":"PT3S", "code":7 } ],
			"tags":[ { "name":"totalCount", "mode":"production" }, { "name":"status", "mode":"toggle", "period":"PT1H" } ] }])" );
		auto& pump = y[0];
		pump.Sample( 1min );//stopped [10,13) [20,23) [30,33) [40,43) [50,53), and from 60
		EXPECT_EQ( pump.Tags[0].Value, 60-5*3 );
		EXPECT_EQ( pump.Code, 7 ); EXPECT_FALSE( pump.On );//as of 60
		pump.Sample( 7s );
		EXPECT_EQ( pump.Tags[0].Value, 60-5*3+4 ); EXPECT_EQ( pump.Code, 0 ); EXPECT_TRUE( pump.On );
	}
	TEST( DeviceTests, ARandomStopsGapsAverageItsMeanAndItsLengthsStayInside ){
		auto y = devices( R"([{ "path":"machine", "seed":478, "stops":[ { "mean":"PT40M", "min":"PT2M", "max":"PT6M", "code":3 } ], "tags":[ { "name":"status", "mode":"on" } ] }])" );
		let stops = stopWindows( y[0], 1000 );
		Duration gaps{}, end{}, shortest{ Duration::max() }, longest{};
		for( let& [begin, stopEnd] : stops ){
			ASSERT_GE( begin, end );
			gaps += begin-end;
			let length = stopEnd-begin;
			ASSERT_GE( length, 2min ); ASSERT_LE( length, 6min );
			shortest = std::min( shortest, length ); longest = std::max( longest, length );
			end = stopEnd;
		}
		EXPECT_NEAR( duration<double>(gaps/1000).count(), 2400, 240 );//a thousand exponential gaps:  sd 76 s
		EXPECT_LT( shortest, 150s ); EXPECT_GT( longest, 330s );//and the lengths are spread
	}
	TEST( DeviceTests, ASeedRepeatsItsStopsAndAnotherDiffers ){
		let seeded = []( sv seed ){ return machines( Ƒ(R"({} "stops":[ {{ "mean":"PT40M", "min":"PT2M", "max":"PT6M" }} ])", seed) ); };
		auto first = seeded( R"("seed":1,)" ); auto again = seeded( R"("seed":1,)" ); auto other = seeded( R"("seed":2,)" );
		let stops = stopWindows( first[0], 20 );
		EXPECT_EQ( stops, stopWindows(again[0], 20) );
		EXPECT_NE( stops, stopWindows(other[0], 20) );
		//Another run's too:  a std engine under absl's distributions, where a seeded absl::BitGen is salted per process.  An abseil
		//that draws either distribution another way moves these, and they are pinned again.
		let millis = []( Duration d ){ return duration_cast<milliseconds>( d ).count(); };
		EXPECT_EQ( millis(stops[0].first), 344'947 ); EXPECT_EQ( millis(stops[0].second), 497'684 );
		EXPECT_EQ( millis(stops[1].first), 1'937'800 ); EXPECT_EQ( millis(stops[1].second), 2'062'846 );

		//no seed:  the start time and the device's place in the list
		constexpr sv pair{ R"([
			{ "path":"a", "stops":[ { "mean":"PT40M", "min":"PT2M", "max":"PT6M" } ], "tags":[ { "name":"status", "mode":"toggle" } ] },
			{ "path":"b", "stops":[ { "mean":"PT40M", "min":"PT2M", "max":"PT6M" } ], "tags":[ { "name":"status", "mode":"toggle" } ] }
		])" };
		auto unseeded = devices( pair ); auto sameStart = devices( pair ); auto later = devices( pair, {}, _start+1s );
		let a = stopWindows( unseeded[0], 20 );
		EXPECT_NE( a, stopWindows(unseeded[1], 20) );
		EXPECT_EQ( a, stopWindows(sameStart[0], 20) );
		EXPECT_NE( a, stopWindows(later[0], 20) );
	}
	TEST( DeviceTests, AnAtStopStartsAtItsTimeInItsZone ){
		constexpr sv changeover{ R"("stops":[ { "at":"06:00", "for":"PT20M", "code":4 } ])" };
		let stopsAfter = []( Device& machine, Duration run ){//on for `run`, to the second, then stopped for the twenty minutes
			machine.Sample( run-1s );
			EXPECT_TRUE( machine.On ); EXPECT_EQ( machine.Code, 0 );
			machine.Sample( 1s );
			EXPECT_FALSE( machine.On ); EXPECT_EQ( machine.Code, 4 );
			machine.Sample( 20min-1s );
			EXPECT_FALSE( machine.On );
			machine.Sample( 1s );
			EXPECT_TRUE( machine.On ); EXPECT_EQ( machine.Code, 0 );
		};
		//New York's midnight on the day its clocks go back at 02:00:  06:00 is seven hours on, and the next day's a day after that.
		auto fall = machines( changeover, Chrono::ToTimePoint("2026-11-01T04:00:00Z"), "America/New_York" );
		stopsAfter( fall[0], 7h );
		stopsAfter( fall[0], 24h-20min );
		//and on the day they go forward:  five hours on, then 02:30, which that day lacks - the instant they jump to, 03:00.
		auto spring = machines( changeover, Chrono::ToTimePoint("2026-03-08T05:00:00Z"), "America/New_York" );
		stopsAfter( spring[0], 5h );
		auto skipped = machines( R"("stops":[ { "at":"02:30", "for":"PT20M", "code":4 } ])", Chrono::ToTimePoint("2026-03-08T05:00:00Z"), "America/New_York" );
		stopsAfter( skipped[0], 2h );
		stopsAfter( skipped[0], 24h-20min-30min );//the next day has its 02:30, 23.5 hours after that 03:00
		//another zone's 06:00, and the emulator's default, UTC
		auto utc = machines( changeover, Chrono::ToTimePoint("2026-11-01T04:00:00Z") );//left out
		stopsAfter( utc[0], 2h );

		auto underWay = machines( changeover, Chrono::ToTimePoint("2026-07-01T10:05:00Z"), "America/New_York" );//06:05:  five minutes into it
		underWay[0].Sample( 1s );
		EXPECT_FALSE( underWay[0].On ); EXPECT_EQ( underWay[0].Code, 4 );
		underWay[0].Sample( 15min-1s );
		EXPECT_TRUE( underWay[0].On );
	}
	TEST( DeviceTests, ASlowPeriodStretchesTheCycleAndSpeedLagsTowardsIt ){
		auto y = machines( R"("slow":[ { "every":"PT200S", "for":"PT100S", "factor":2 } ])" );//slow [200,300) [400,500)
		auto& machine = y[0];
		let& speed = machine.Tags[0]; let& count = machine.Tags[1];
		EXPECT_FALSE( machine.ReasonTag );//no stops, no code
		machine.Sample( 200s );
		EXPECT_EQ( count.Value, 200 ); EXPECT_NEAR( speed.Value, 60, 1e-6 );//units a minute at a cycle of a second
		EXPECT_TRUE( machine.On );
		machine.Sample( 1s );
		EXPECT_NEAR( speed.Value, 30+30*std::exp(-1.0), 1e-6 );
		machine.Sample( 99s );
		EXPECT_EQ( count.Value, 250 ); EXPECT_NEAR( speed.Value, 30, 1e-6 );//a unit every two seconds
		EXPECT_TRUE( machine.On );
		machine.Sample( 1s );
		EXPECT_NEAR( speed.Value, 60-30*std::exp(-1.0), 1e-6 );
		machine.Sample( 199s );//one period over the next, whole:  cut at both its edges
		EXPECT_EQ( count.Value, 251+99+50 );
	}
	TEST( DeviceTests, ACounterStepsOnceAPeriodWhateverItsPieces ){
		auto y = devices( R"([{ "path":"machine", "slow":[ { "every":"PT10S", "for":"PT2S", "factor":2 } ], "tags":[ { "name":"units", "mode":"production" }, { "name":"ticks", "mode":"counter" }, { "name":"walk", "mode":"randomWalk" } ] }])" );
		auto& machine = y[0];
		let& ticks = machine.Tags[1]; let& walk = machine.Tags[2];
		for( uint period=1; period<=20; ++period ){//1.5 s each:  the slow edges at 10, 20 and 22 s cut the periods they fall in
			let before = walk.Value;
			machine.Sample( 1500ms );
			EXPECT_EQ( ticks.Value, period ) << period;
			if( period>1 )//the first is the walk's start, (min+max)/2
				EXPECT_LE( std::abs(walk.Value-before), 1 ) << period;//a step of 1 at most
		}
	}
	TEST( DeviceTests, AnUncodedStopNeverHidesACodedOnesReason ){
		auto y = machines( R"("stops":[ { "every":"PT20S", "for":"PT10S" }, { "every":"PT25S", "for":"PT10S", "code":5 } ])" );//[20,30) with none, [25,35) with its code
		auto& machine = y[0];
		for( uint second=1; second<=39; ++second ){
			machine.Sample( 1s );
			let uncoded = second>=20 && second<30; let coded = second>=25 && second<35;
			EXPECT_EQ( machine.On, !uncoded && !coded ) << second;
			EXPECT_EQ( machine.Code, coded ? 5 : 0 ) << second;
		}
	}
	TEST( DeviceTests, AnAtStopKeepsToTheWallClock ){//through a pause the steady clock missed:  a suspend, a VM paused
		constexpr sv stops{ R"("stops":[ { "at":"06:00", "for":"PT20M", "code":4 }, { "every":"PT10H", "for":"PT1M", "code":5 } ])" };
		auto y = machines( stops );//from midnight, UTC
		auto& machine = y[0];
		machine.Sample( 1h );
		machine.Anchor( _start+1h+3min );//the wall clock three minutes on
		EXPECT_EQ( machine.Stops[1].Begin, 10h );//the device clock's shapes stay where they were
		machine.Sample( 5h-3min-1s );//05:59:59 on the wall clock
		EXPECT_TRUE( machine.On );
		machine.Sample( 1s );
		EXPECT_FALSE( machine.On ); EXPECT_EQ( machine.Code, 4 );

		auto paused = machines( stops );
		auto& through = paused[0];
		through.Sample( 5h );
		through.Anchor( _start+5h+90min );//06:30:  the pause ran past the stop
		EXPECT_EQ( through.Stops[0].Begin, 24h+6h-90min );//the next day's, on the device clock
		through.Sample( 1s );
		EXPECT_TRUE( through.On ); EXPECT_EQ( through.Code, 0 );
	}
	TEST( DeviceTests, AnUncodedStopAfterACodedOneReadsNoCode ){
		auto y = machines( R"("stops":[ { "every":"PT20S", "for":"PT5S", "code":7 }, { "every":"PT30S", "for":"PT5S" } ])" );//[20,25) with its code, [30,35) with none
		auto& machine = y[0];
		ASSERT_TRUE( machine.ReasonTag );
		let& speed = machine.Tags[0]; let& reason = machine.Tags[*machine.ReasonTag];
		for( uint second=1; second<=39; ++second ){
			machine.Sample( 1s );
			let coded = second>=20 && second<25; let uncoded = second>=30 && second<35;
			EXPECT_EQ( machine.On, !coded && !uncoded ) << second;
			EXPECT_EQ( machine.Code, coded ? 7 : 0 ) << second;
			EXPECT_EQ( reason.Value, coded ? 7 : 0 ) << second;
			if( second==20 )
				EXPECT_NEAR( speed.Value, 60, 1e-6 );
			if( second==25 )
				EXPECT_NEAR( speed.Value, 60*std::exp(-5.0), 1e-6 );//towards 0 through the stop
		}
	}
	TEST( DeviceTests, AHeldFaultOnTheCountHoldsItsRejects ){//one sensor:  never more rejects than units in an interval
		auto y = devices( R"([{ "path":"pump1", "tags":[ { "name":"totalCount", "mode":"production", "reject":1, "quality":[{ "status":"BadSensorFailure", "start":"PT2S", "duration":"PT2S" }] } ] }])" );
		auto& pump = y[0];
		let& count = pump.Tags[0]; let& rejects = pump.Tags[1];
		for( uint second=1; second<=5; ++second ){
			pump.Sample( 1s );
			let held = second==2 || second==3;
			EXPECT_EQ( count.Value, held ? 1 : second ) << second; EXPECT_EQ( rejects.Value, count.Value ) << second;
			EXPECT_EQ( count.Status, held ? UA_STATUSCODE_BADSENSORFAILURE : UA_STATUSCODE_GOOD ) << second; EXPECT_EQ( rejects.Status, count.Status ) << second;
		}
	}
	TEST( ParseDevicesTests, TwoTagsOfOneNameAreRefused ){
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"a", "mode":"production", "reject":0.1 }, { "name":"b", "mode":"production", "reject":0.1 } ] }])"), Exception );//both `rejectCount`
		EXPECT_NO_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"a", "mode":"production", "reject":0.1 }, { "name":"b", "mode":"production", "reject":0.1, "rejectTag":"bRejects" } ] }])") );
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"status", "mode":"command" }, { "name":"n", "mode":"production", "reject":0.1, "rejectTag":"status" } ] }])"), Exception );
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"x" }, { "name":"x", "mode":"sine" } ] }])"), Exception );
	}
	TEST( ParseDevicesTests, ACommandTagBesideAGeneratedStatusIsRefused ){//it would command nothing
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"run", "mode":"command" }, { "name":"status", "mode":"toggle" } ] }])"), Exception );
		EXPECT_NO_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"run", "mode":"command" }, { "name":"motorRpm", "mode":"follow" } ] }])") );
	}
	TEST( ParseDevicesTests, StopRefusals ){
		constexpr sv tags{ R"("tags":[ { "name":"status", "mode":"toggle" } ])" };
		let stops = [&]( sv list, sv tags_ ){ return devices( Ƒ(R"([{{ "path":"pump2", "stops":{}, {} }}])", list, tags_) ); };
		EXPECT_NO_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", tags) );
		EXPECT_NO_THROW( stops("[]", R"("tags":[ { "name":"status", "mode":"command" } ])") );//no stops, nothing to hold
		EXPECT_THROW( stops(R"({ "every":"PT10S", "for":"PT3S", "code":7 })", tags), Exception );//not a list
		EXPECT_NO_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S" }])", tags) );//no code:  a stop with no reason
		EXPECT_NO_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":0 }])", tags) );
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT10S", "code":7 }])", tags), Exception );//never running
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "code":7 }])", tags), Exception );
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", R"("tags":[ { "name":"status", "mode":"command" } ])"), Exception );//the UI's status is not the emulator's to hold
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", R"("tags":[ { "name":"motorRpm", "mode":"sine" } ])"), Exception );//no status
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":7 }])", R"("tags":[ { "name":"status", "mode":"toggle" }, { "name":"reasonCode" } ])"), Exception );//the stops' own tag
		EXPECT_NO_THROW( stops("[]", R"("tags":[ { "name":"status", "mode":"toggle" }, { "name":"reasonCode" } ])") );//no stops, no tag of theirs
		EXPECT_THROW( stops(R"([{ "every":"60s", "for":"PT3S", "code":7 }])", tags), Exception );//not ISO 8601
		EXPECT_THROW( stops(R"([{ "every":"PT10S", "for":"PT3S", "code":"7" }])", tags), Exception );
	}
	TEST( ParseDevicesTests, StopShapeRefusals ){
		let stops = []( sv list ){ return machines( Ƒ(R"("stops":{})", list) ); };
		EXPECT_NO_THROW( stops(R"([{ "mean":"PT10M", "min":"PT2M", "max":"PT6M", "code":3 }, { "at":"06:00", "for":"PT20M" }, { "at":"18:30:15", "for":"PT1M" }])") );
		EXPECT_NO_THROW( stops(R"([{ "mean":"PT10M", "min":"PT2M", "max":"PT2M" }])") );//one length
		EXPECT_THROW( stops(R"([{ "mean":"PT10M", "min":"PT2M", "max":"PT1M" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "mean":"PT10M", "min":"PT2M" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "mean":"PT10M", "max":"PT2M" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "mean":"PT0S", "min":"PT2M", "max":"PT6M" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "mean":600, "min":"PT2M", "max":"PT6M" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "every":"PT1H", "for":"PT3S", "mean":"PT10M", "min":"PT2M", "max":"PT6M" }])"), Exception );//two shapes
		EXPECT_THROW( stops(R"([{ "at":"06:00", "every":"PT1H", "for":"PT3S" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "for":"PT3S", "code":7 }])"), Exception );//none
		EXPECT_THROW( stops(R"([{ "at":"06:00" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "at":"06:00", "for":"PT23H" }])"), Exception );//the day the clocks go forward:  it would never release
		EXPECT_NO_THROW( stops(R"([{ "at":"06:00", "for":"PT22H59M" }])") );
		EXPECT_THROW( stops(R"([{ "every":"PT1H", "for":"PT10M", "factor":2 }])"), Exception );//a slow period's key:  never a stop unread
		EXPECT_THROW( stops(R"([{ "mean":"PT10M", "min":"PT2M", "max":"PT6M", "for":"PT3M" }])"), Exception );//another shape's
		EXPECT_THROW( stops(R"([{ "every":"PT1H", "for":"PT10M", "min":"PT2M" }])"), Exception );
		EXPECT_THROW( stops(R"([{ "at":"06:00", "for":"PT10M", "cod":4 }])"), Exception );//a typo
		for( let at : {"6:00", "24:00", "06:60", "06:00:60", "06.00", "0600", "06:00:", "06:00:00Z", "", "6am"} )
			EXPECT_THROW( stops(Ƒ(R"([{{ "at":"{}", "for":"PT20M" }}])", at)), Exception ) << at;
		EXPECT_THROW( stops(R"([{ "at":600, "for":"PT20M" }])"), Exception );

		EXPECT_NO_THROW( machines(R"("seed":18446744073709551615)") );
		EXPECT_THROW( machines(R"("seed":"1")"), Exception );
		EXPECT_THROW( machines(R"("seed":-1)"), Exception );
		EXPECT_THROW( machines(R"("seed":1.5)"), Exception );
	}
	TEST( ParseDevicesTests, SlowRefusals ){
		let slow = []( sv list ){ return machines( Ƒ(R"("slow":{})", list) ); };
		EXPECT_NO_THROW( slow(R"([{ "every":"PT1H", "for":"PT30M", "factor":1.25 }, { "mean":"PT1H", "min":"PT1M", "max":"PT5M", "factor":3 }, { "at":"14:00", "for":"PT30M", "factor":1.25 }])") );
		EXPECT_NO_THROW( slow("[]") );
		EXPECT_THROW( slow(R"({ "every":"PT1H", "for":"PT30M", "factor":1.25 })"), Exception );//not a list
		EXPECT_THROW( slow(R"([{ "every":"PT1H", "for":"PT30M" }])"), Exception );//no factor
		EXPECT_THROW( slow(R"([{ "every":"PT1H", "for":"PT30M", "factor":1 }])"), Exception );//not slow
		EXPECT_THROW( slow(R"([{ "every":"PT1H", "for":"PT30M", "factor":0.8 }])"), Exception );
		EXPECT_THROW( slow(R"([{ "every":"PT1H", "for":"PT30M", "factor":"1.25" }])"), Exception );
		EXPECT_THROW( slow(R"([{ "for":"PT30M", "factor":1.25 }])"), Exception );//no shape
		EXPECT_THROW( slow(R"([{ "every":"PT1H", "for":"PT30M", "factor":1.25, "code":3 }])"), Exception );//a stop's key
		constexpr sv noCount{ R"([{ "path":"pump1", "slow":[{ "every":"PT1H", "for":"PT30M", "factor":1.25 }], "tags":[ { "name":"motorRpm", "mode":"follow" }, { "name":"status", "mode":"command" } ] }])" };
		EXPECT_THROW( devices(noCount), Exception );//nothing to slow
	}
	TEST( ParseDevicesTests, TheZoneIsLookedUpForAnAtEntryAlone ){//so a host without the tz database runs the rest
		EXPECT_EQ( machines(R"("stops":[ { "every":"PT1H", "for":"PT10M" } ])")[0].Zone, nullptr );
		EXPECT_EQ( machines(R"("stops":[ { "at":"06:00", "for":"PT10M" } ])")[0].Zone->name(), std::chrono::locate_zone("UTC")->name() );//left out
		EXPECT_EQ( machines(R"("slow":[ { "at":"06:00", "for":"PT10M", "factor":2 } ])", _start, "America/New_York")[0].Zone->name(), "America/New_York" );
	}
	TEST( ParseDevicesTests, AGapIsTheShortestRunBetweenTwoWindows ){//what the startup warning holds to the period
		let y = machines( R"("stops":[ { "every":"PT1H", "for":"PT10M" }, { "at":"06:00", "for":"PT22H" }, { "mean":"PT40M", "min":"PT2M", "max":"PT6M" } ])" );
		EXPECT_EQ( y[0].Stops[0].Gap(), 50min );
		EXPECT_EQ( y[0].Stops[1].Gap(), 1h );//on the day the clocks go forward
		EXPECT_EQ( y[0].Stops[2].Gap(), 40min );//the mean:  an exponential gap is most often shorter
	}
	TEST( ParseDevicesTests, AFollowsTagTakesItsProductionTagsPace ){
		auto y = devices( R"([{ "path":"machine", "tags":[ { "name":"speed", "mode":"follow", "follows":"units" }, { "name":"units", "mode":"production", "cycle":"PT24S" }, { "name":"motorRpm", "mode":"follow" } ] }])" );
		EXPECT_EQ( y[0].Tags[0].Spec.RatedRpm, 2.5 );//units a minute, though it is listed before its count
		EXPECT_EQ( y[0].Tags[2].Spec.RatedRpm, 1450 );
		EXPECT_THROW( devices(R"([{ "path":"machine", "tags":[ { "name":"speed", "mode":"follow", "follows":"motorRpm" }, { "name":"motorRpm", "mode":"sine" } ] }])"), Exception );//not a count
		EXPECT_THROW( devices(R"([{ "path":"machine", "tags":[ { "name":"speed", "mode":"follow", "follows":"units" } ] }])"), Exception );//no such tag
		EXPECT_THROW( devices(R"([{ "path":"machine", "tags":[ { "name":"speed", "mode":"follow", "follows":"rejectCount" }, { "name":"units", "mode":"production", "reject":0.1 } ] }])"), Exception );//the count's second tag is not a count
	}
	TEST( ParseDevicesTests, Refusals ){
		EXPECT_THROW( devices("[]"), Exception );
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[] }])"), Exception );
		EXPECT_THROW( devices(R"([{ "tags":[ { "name":"motorRpm" } ] }])"), Exception );//no path
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"motorRpm", "mode":"randomWalk", "step":0 } ] }])"), Exception );//#9 propagates
		EXPECT_THROW( devices(R"([{ "path":"pump1", "tags":[ { "name":"status", "mode":"command", "quality":[{ "status":"Bad", "duration":"PT1S" }] } ] }])"), Exception );//so does a quality nothing would write
	}
}
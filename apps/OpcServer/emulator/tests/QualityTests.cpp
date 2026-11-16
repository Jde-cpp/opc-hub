//Quality:  the OPC 10000-4 7.38 StatusCode - names, the bit layout, the scheduled windows and the sensor range.
#include "../Devices.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	Ω spec( sv json )ε->TagSpec{ return TagSpec{ boost::json::parse(string{json}).as_object() }; }
	//A tag's quality on a device clock of its own, stepped by each Apply's dt, as Device::Sample steps the device's.
	struct Quality{
		Quality( sv json, uint64_t seed=0, uint32_t place=0 )ε{ let s = spec( json ); Q = TagQuality{ s.Quality, s.SensorMin, s.SensorMax, seed, place }; }
		α Apply( Duration dt, double raw, double& value )ι->UA_StatusCode{ D.Clock += dt; return Q.Apply( D, raw, value ); }
		Device D; TagQuality Q;
	};

	TEST( ParseStatusTests, EveryNameIsTheStacksOwnSpelling ){//so what the config says is what the log and the UI say back.
		for( let& [name, code] : StatusNames() ){
			EXPECT_EQ( ParseStatus(name), code ) << name;
			EXPECT_EQ( sv{UA_StatusCode_name(code)}, name );
		}
	}
	TEST( ParseStatusTests, AnyCodeByNumber ){
		EXPECT_EQ( ParseStatus("0x808C0000"), UA_STATUSCODE_BADSENSORFAILURE );
		EXPECT_EQ( ParseStatus("0X80340000"), UA_STATUSCODE_BADNODEIDUNKNOWN );//the rest of Table 178 - not a quality, still expressible
		EXPECT_EQ( ParseStatus("2156658688"), UA_STATUSCODE_BADSENSORFAILURE );
		EXPECT_EQ( ParseStatus("0"), UA_STATUSCODE_GOOD );
	}
	TEST( ParseStatusTests, Refusals ){
		EXPECT_THROW( ParseStatus("BadSensorFailed"), Exception );
		EXPECT_THROW( ParseStatus("badSensorFailure"), Exception );//names are case sensitive, like the modes
		EXPECT_THROW( ParseStatus(""), Exception );
		EXPECT_THROW( ParseStatus("0x"), Exception );
		EXPECT_THROW( ParseStatus("0x808C0000z"), Exception );
		EXPECT_THROW( ParseStatus("0x1FFFFFFFF"), Exception );//33 bits
		EXPECT_THROW( ParseStatus("0xC0000000"), Exception );//severity 11 is reserved
		EXPECT_THROW( ParseLimit("middle"), Exception );
	}

	//Tables 176/177, bit for bit.
	TEST( ComposeTests, TheLayoutIsTheSpecs ){
		EXPECT_EQ( Compose(UA_STATUSCODE_GOOD, {}), 0u );
		EXPECT_EQ( Compose(UA_STATUSCODE_BADSENSORFAILURE, {}), UA_STATUSCODE_BADSENSORFAILURE );//no flags, no InfoType
		EXPECT_EQ( Compose(UA_STATUSCODE_GOOD, {.StructureChanged=true}), 0x8000u );
		EXPECT_EQ( Compose(UA_STATUSCODE_GOOD, {.SemanticsChanged=true}), 0x4000u );
		//a limit or overflow bit means nothing unless InfoType (11:10) says DataValue (01)
		EXPECT_EQ( Compose(UA_STATUSCODE_GOOD, {.Limit=ELimit::Low}), 0x0400u | 0x0100u );
		EXPECT_EQ( Compose(UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED, {.Limit=ELimit::High}), 0x40940000u | 0x0400u | 0x0200u );
		EXPECT_EQ( Compose(UA_STATUSCODE_BADSENSORFAILURE, {.Limit=ELimit::Constant}), 0x808C0000u | 0x0400u | 0x0300u );
		EXPECT_EQ( Compose(UA_STATUSCODE_GOOD, {.Overflow=true}), 0x0400u | 0x0080u );
		EXPECT_EQ( Compose(UA_STATUSCODE_BAD, {ELimit::Constant, true, true, true}), 0x80000000u | 0x8000u | 0x4000u | 0x0400u | 0x0300u | 0x0080u );
	}
	TEST( ComposeTests, ALimitReplacesTheCodesOwn ){
		let high = Compose( UA_STATUSCODE_UNCERTAIN, {.Limit=ELimit::High} );
		EXPECT_EQ( Compose(high, {.Limit=ELimit::Low}), UA_STATUSCODE_UNCERTAIN | 0x0400u | 0x0100u );
		EXPECT_EQ( Compose(high, {}), high );//none leaves it alone
	}
	TEST( ComposeTests, SeverityAndNameSurviveTheFlags ){
		let sc = Compose( UA_STATUSCODE_BADSENSORFAILURE, {ELimit::High, true, true, true} );
		EXPECT_TRUE( UA_StatusCode_isBad(sc) );
		EXPECT_EQ( ToString(sc), "BadSensorFailure+High+Overflow+StructureChanged+SemanticsChanged" );
		EXPECT_EQ( ToString(UA_STATUSCODE_GOOD), "Good" );
		EXPECT_EQ( ToString(Compose(UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE, {.Limit=ELimit::Constant})), "UncertainLastUsableValue+Constant" );
	}

	TEST( QualityWindowTests, HoldDefaultsOnForBadAndLastUsableValue ){
		let s = spec( R"({"name":"x","mode":"sine","quality":[
			{"status":"BadSensorFailure","for":"PT1S"}, {"status":"UncertainLastUsableValue","for":"PT1S"}, {"status":"UncertainNoCommunicationLastUsableValue","for":"PT1S"},
			{"status":"UncertainSensorNotAccurate","for":"PT1S"}, {"status":"GoodLocalOverride","for":"PT1S"},
			{"status":"BadSensorFailure","for":"PT1S","hold":false}, {"status":"UncertainSubNormal","for":"PT1S","hold":true} ]})" );
		ASSERT_EQ( s.Quality.size(), 7u );
		EXPECT_TRUE( s.Quality[0].Hold ); EXPECT_TRUE( s.Quality[1].Hold ); EXPECT_TRUE( s.Quality[2].Hold );
		EXPECT_FALSE( s.Quality[3].Hold ); EXPECT_FALSE( s.Quality[4].Hold );
		EXPECT_FALSE( s.Quality[5].Hold ); EXPECT_TRUE( s.Quality[6].Hold );//said outright beats the default
	}
	TEST( QualityWindowTests, StatusByNameStringOrNumberWithItsFlags ){
		let s = spec( R"({"name":"x","quality":[
			{"status":"0x80310000","for":"PT1S","limit":"constant","overflow":true,"structureChanged":true,"semanticsChanged":true},
			{"status":2156658688,"for":"PT1S"} ]})" );
		EXPECT_EQ( s.Quality[0].Status, 0x80310000u | 0x8000u | 0x4000u | 0x0400u | 0x0300u | 0x0080u );
		EXPECT_TRUE( s.Quality[0].HasLimit );
		EXPECT_EQ( s.Quality[1].Status, UA_STATUSCODE_BADSENSORFAILURE );
		EXPECT_FALSE( s.Quality[1].HasLimit );
	}
	TEST( QualityWindowTests, OnceFromStartForItsLengthOrAgainEveryEvery ){
		Device device; std::mt19937_64 engine;
		auto once = spec( R"({"name":"x","quality":[{"status":"Bad","start":"PT10S","for":"PT5S"}]})" ).Quality[0];
		EXPECT_EQ( once.Shape, Recurrence::EShape::Once );
		EXPECT_FALSE( once.IsActive(9s) ); EXPECT_TRUE( once.IsActive(10s) ); EXPECT_TRUE( once.IsActive(14s) ); EXPECT_FALSE( once.IsActive(15s) );
		EXPECT_EQ( once.NextEdge(0s), 10s ); EXPECT_EQ( once.NextEdge(10s), 15s ); EXPECT_EQ( once.NextEdge(14s), 15s );
		once.Advance( device, engine );//one shot:  no window left
		EXPECT_FALSE( once.IsActive(15s) ); EXPECT_FALSE( once.IsActive(70s) ); EXPECT_EQ( once.NextEdge(15s), Duration::max() ); EXPECT_FALSE( once.Gap() );
		auto repeating = spec( R"({"name":"x","quality":[{"status":"Bad","start":"PT10S","for":"PT5S","every":"PT1M"}]})" ).Quality[0];
		EXPECT_TRUE( repeating.IsActive(12s) ); EXPECT_FALSE( repeating.IsActive(15s) );
		repeating.Advance( device, engine );
		EXPECT_FALSE( repeating.IsActive(69s) ); EXPECT_TRUE( repeating.IsActive(70s) ); EXPECT_TRUE( repeating.IsActive(74s) ); EXPECT_FALSE( repeating.IsActive(75s) ); EXPECT_EQ( repeating.Gap(), 55s );
		let fromEvery = spec( R"({"name":"x","quality":[{"status":"Bad","for":"PT5S","every":"PT1M"}]})" ).Quality[0];//no start:  the first at `every`, as a stop's
		EXPECT_FALSE( fromEvery.IsActive(0s) ); EXPECT_FALSE( fromEvery.IsActive(59s) ); EXPECT_TRUE( fromEvery.IsActive(60s) );
		let fromZero = spec( R"({"name":"x","quality":[{"status":"Bad","for":"PT5S"}]})" ).Quality[0];//a one-shot's at 0
		EXPECT_TRUE( fromZero.IsActive(0s) ); EXPECT_FALSE( fromZero.IsActive(5s) );
	}
	TEST( QualityWindowTests, Refusals ){
		EXPECT_THROW( spec(R"({"name":"x","quality":{"status":"Bad","for":"PT1S"}})"), Exception );//not an array
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"for":"PT1S"}]})"), Exception );//no status
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Worse","for":"PT1S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad"}]})"), Exception );//no for
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT0S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT10S","every":"PT5S"}]})"), Exception );//would never end
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT10S","every":"PT10S"}]})"), Exception );//so would this:  a fault that stays is a one-shot with a long `for`
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","start":"PT-1S","for":"PT1S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","duration":"PT1S"}]})"), Exception );//the key it took before #658:  a typo now
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT1S","code":1}]})"), Exception );//a stop's key
		//the stops' shapes, with a `start` on the clock's two alone
		EXPECT_NO_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","mean":"PT1M","min":"PT1S","max":"PT2S"}, {"status":"Bad","at":"06:00","for":"PT1M"}]})") );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT1S","mean":"PT1M","min":"PT1S","max":"PT2S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","start":"PT1S","mean":"PT1M","min":"PT1S","max":"PT2S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","start":"PT1S","at":"06:00","for":"PT1M"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT1S","limit":"middle"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"5s"}]})"), Exception );//a key of the wrong kind, not its default
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT1S","every":60}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","for":"PT1S","hold":"no"}]})"), Exception );
		//the emulator never writes a command tag, and a bool has no range
		EXPECT_THROW( spec(R"({"name":"x","mode":"command","quality":[{"status":"Bad","for":"PT1S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"command","sensorMax":1})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"toggle","sensorMax":1})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","sensorMin":10,"sensorMax":10})"), Exception );
		EXPECT_NO_THROW( spec(R"({"name":"x","mode":"toggle","quality":[{"status":"Bad","for":"PT1S"}]})") );//a bool still has a quality
	}

	TEST( TagQualityTests, GoodWithoutAWindow ){
		Quality q{ R"({"name":"x"})" };
		double value{};
		EXPECT_EQ( q.Apply(1s, 42, value), UA_STATUSCODE_GOOD );
		EXPECT_EQ( value, 42 );
	}
	TEST( TagQualityTests, AHoldingWindowFreezesTheReadingAndReleasesIt ){
		Quality q{ R"({"name":"x","quality":[{"status":"BadSensorFailure","start":"PT2S","for":"PT2S"}]})" };
		double value{};
		EXPECT_EQ( q.Apply(1s, 10, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 10 );//t=1
		EXPECT_EQ( q.Apply(1s, 20, value), UA_STATUSCODE_BADSENSORFAILURE ); EXPECT_EQ( value, 10 );//t=2: the last reading from before the fault
		EXPECT_EQ( q.Apply(1s, 30, value), UA_STATUSCODE_BADSENSORFAILURE ); EXPECT_EQ( value, 10 );
		EXPECT_EQ( q.Apply(1s, 40, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 40 );//t=4: the process ran on meanwhile
	}
	TEST( TagQualityTests, AWindowThatDoesNotHoldKeepsReading ){
		Quality q{ R"({"name":"x","quality":[{"status":"UncertainSensorNotAccurate","for":"PT5S"}]})" };
		double value{};
		EXPECT_EQ( q.Apply(1s, 10, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE ); EXPECT_EQ( value, 10 );
		EXPECT_EQ( q.Apply(1s, 20, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE ); EXPECT_EQ( value, 20 );
	}
	TEST( TagQualityTests, TheFirstActiveWindowWins ){
		Quality q{ R"({"name":"x","quality":[{"status":"UncertainSubNormal","start":"PT2S","for":"PT1S"}, {"status":"BadDeviceFailure","for":"PT10S"}]})" };
		double value{};
		EXPECT_EQ( q.Apply(1s, 1, value), UA_STATUSCODE_BADDEVICEFAILURE );
		EXPECT_EQ( q.Apply(1s, 1, value), UA_STATUSCODE_UNCERTAINSUBNORMAL );
		EXPECT_EQ( q.Apply(1s, 1, value), UA_STATUSCODE_BADDEVICEFAILURE );
	}
	TEST( TagQualityTests, AReadingOutsideTheSensorRangeIsPinnedWithItsLimitBit ){
		Quality q{ R"({"name":"x","mode":"sine","sensorMin":100,"sensorMax":200})" };
		double value{};
		EXPECT_EQ( q.Apply(1s, 150, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 150 );
		EXPECT_EQ( q.Apply(1s, 250, value), Compose(UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED, {.Limit=ELimit::High}) ); EXPECT_EQ( value, 200 );
		EXPECT_EQ( q.Apply(1s, 50, value), Compose(UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED, {.Limit=ELimit::Low}) ); EXPECT_EQ( value, 100 );
		EXPECT_EQ( q.Apply(1s, 200, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 200 );//at the range is inside it
	}
	TEST( TagQualityTests, AWindowTakesTheRangesLimitUnlessItNamesItsOwn ){
		Quality inherits{ R"({"name":"x","mode":"sine","sensorMax":200,"quality":[{"status":"UncertainSensorNotAccurate","for":"PT10S"}]})" };
		double value{};
		EXPECT_EQ( inherits.Apply(1s, 250, value), Compose(UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE, {.Limit=ELimit::High}) ); EXPECT_EQ( value, 200 );
		EXPECT_EQ( inherits.Apply(1s, 150, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE );
		Quality own{ R"({"name":"x","mode":"sine","sensorMax":200,"quality":[{"status":"UncertainSensorNotAccurate","for":"PT10S","limit":"none"}]})" };
		EXPECT_EQ( own.Apply(1s, 250, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE ); EXPECT_EQ( value, 200 );
	}
	TEST( TagQualityTests, ARandomWindowDrawsFromTheTagsOwnSeed ){
		let statuses = []( uint64_t seed, uint32_t place ){
			Quality q{ R"({"name":"x","quality":[{"status":"Bad","mean":"PT10S","min":"PT2S","max":"PT4S"}]})", seed, place };
			double value{}; vector<bool> y;
			for( uint second=0; second<600; ++second )
				y.push_back( UA_StatusCode_isBad(q.Apply(1s, 1, value)) );
			return y;
		};
		let a = statuses( 1, 0 );
		EXPECT_EQ( a, statuses(1, 0) );
		EXPECT_NE( a, statuses(2, 0) ); EXPECT_NE( a, statuses(1, 1) );//another device's seed, another place in the list
		let bad = std::ranges::count( a, true );
		EXPECT_GT( bad, 60 ); EXPECT_LT( bad, 240 );//3 s in every 13 or so
	}
	TEST( TagQualityTests, AHeldReadingKeepsTheLimitItWasTakenAt ){
		Quality q{ R"({"name":"x","mode":"sine","sensorMax":200,"quality":[{"status":"BadSensorFailure","start":"PT2S","for":"PT5S"}]})" };
		double value{};
		q.Apply( 1s, 250, value );
		EXPECT_EQ( q.Apply(1s, 150, value), Compose(UA_STATUSCODE_BADSENSORFAILURE, {.Limit=ELimit::High}) ); EXPECT_EQ( value, 200 );
	}
}
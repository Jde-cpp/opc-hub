//Quality:  the OPC 10000-4 7.38 StatusCode - names, the bit layout, the scheduled windows and the sensor range.
#include "../Signals.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	Ω spec( sv json )ε->TagSpec{ return TagSpec{ boost::json::parse(string{json}).as_object() }; }
	Ω quality( sv json )ε->TagQuality{ let s = spec( json ); return TagQuality{ s.Quality, s.SensorMin, s.SensorMax }; }

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
			{"status":"BadSensorFailure","duration":"PT1S"}, {"status":"UncertainLastUsableValue","duration":"PT1S"}, {"status":"UncertainNoCommunicationLastUsableValue","duration":"PT1S"},
			{"status":"UncertainSensorNotAccurate","duration":"PT1S"}, {"status":"GoodLocalOverride","duration":"PT1S"},
			{"status":"BadSensorFailure","duration":"PT1S","hold":false}, {"status":"UncertainSubNormal","duration":"PT1S","hold":true} ]})" );
		ASSERT_EQ( s.Quality.size(), 7u );
		EXPECT_TRUE( s.Quality[0].Hold ); EXPECT_TRUE( s.Quality[1].Hold ); EXPECT_TRUE( s.Quality[2].Hold );
		EXPECT_FALSE( s.Quality[3].Hold ); EXPECT_FALSE( s.Quality[4].Hold );
		EXPECT_FALSE( s.Quality[5].Hold ); EXPECT_TRUE( s.Quality[6].Hold );//said outright beats the default
	}
	TEST( QualityWindowTests, StatusByNameStringOrNumberWithItsFlags ){
		let s = spec( R"({"name":"x","quality":[
			{"status":"0x80310000","duration":"PT1S","limit":"constant","overflow":true,"structureChanged":true,"semanticsChanged":true},
			{"status":2156658688,"duration":"PT1S"} ]})" );
		EXPECT_EQ( s.Quality[0].Status, 0x80310000u | 0x8000u | 0x4000u | 0x0400u | 0x0300u | 0x0080u );
		EXPECT_TRUE( s.Quality[0].HasLimit );
		EXPECT_EQ( s.Quality[1].Status, UA_STATUSCODE_BADSENSORFAILURE );
		EXPECT_FALSE( s.Quality[1].HasLimit );
	}
	TEST( QualityWindowTests, ActiveFromStartForDurationEveryEvery ){
		let once = spec( R"({"name":"x","quality":[{"status":"Bad","start":"PT10S","duration":"PT5S"}]})" ).Quality[0];
		EXPECT_FALSE( once.IsActive(9s) ); EXPECT_TRUE( once.IsActive(10s) ); EXPECT_TRUE( once.IsActive(14s) ); EXPECT_FALSE( once.IsActive(15s) );
		EXPECT_FALSE( once.IsActive(70s) );//one shot
		let repeating = spec( R"({"name":"x","quality":[{"status":"Bad","start":"PT10S","duration":"PT5S","every":"PT1M"}]})" ).Quality[0];
		EXPECT_TRUE( repeating.IsActive(12s) ); EXPECT_FALSE( repeating.IsActive(69s) ); EXPECT_TRUE( repeating.IsActive(70s) ); EXPECT_TRUE( repeating.IsActive(134s) ); EXPECT_FALSE( repeating.IsActive(135s) );
	}
	TEST( QualityWindowTests, Refusals ){
		EXPECT_THROW( spec(R"({"name":"x","quality":{"status":"Bad","duration":"PT1S"}})"), Exception );//not an array
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"duration":"PT1S"}]})"), Exception );//no status
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Worse","duration":"PT1S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad"}]})"), Exception );//no duration
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","duration":"PT0S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","duration":"PT10S","every":"PT5S"}]})"), Exception );//would never end
		EXPECT_THROW( spec(R"({"name":"x","quality":[{"status":"Bad","duration":"PT1S","limit":"middle"}]})"), Exception );
		//the emulator never writes a command tag, and a bool has no range
		EXPECT_THROW( spec(R"({"name":"x","mode":"command","quality":[{"status":"Bad","duration":"PT1S"}]})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"command","sensorMax":1})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"toggle","sensorMax":1})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","sensorMin":10,"sensorMax":10})"), Exception );
		EXPECT_NO_THROW( spec(R"({"name":"x","mode":"toggle","quality":[{"status":"Bad","duration":"PT1S"}]})") );//a bool still has a quality
	}

	TEST( TagQualityTests, GoodWithoutASchedule ){
		TagQuality q;
		double value{};
		EXPECT_EQ( q.Apply(1s, 42, value), UA_STATUSCODE_GOOD );
		EXPECT_EQ( value, 42 );
	}
	TEST( TagQualityTests, AHoldingWindowFreezesTheReadingAndReleasesIt ){
		auto q = quality( R"({"name":"x","quality":[{"status":"BadSensorFailure","start":"PT2S","duration":"PT2S"}]})" );
		double value{};
		EXPECT_EQ( q.Apply(1s, 10, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 10 );//t=1
		EXPECT_EQ( q.Apply(1s, 20, value), UA_STATUSCODE_BADSENSORFAILURE ); EXPECT_EQ( value, 10 );//t=2: the last reading from before the fault
		EXPECT_EQ( q.Apply(1s, 30, value), UA_STATUSCODE_BADSENSORFAILURE ); EXPECT_EQ( value, 10 );
		EXPECT_EQ( q.Apply(1s, 40, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 40 );//t=4: the process ran on meanwhile
	}
	TEST( TagQualityTests, AWindowThatDoesNotHoldKeepsReading ){
		auto q = quality( R"({"name":"x","quality":[{"status":"UncertainSensorNotAccurate","duration":"PT5S"}]})" );
		double value{};
		EXPECT_EQ( q.Apply(1s, 10, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE ); EXPECT_EQ( value, 10 );
		EXPECT_EQ( q.Apply(1s, 20, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE ); EXPECT_EQ( value, 20 );
	}
	TEST( TagQualityTests, TheFirstActiveWindowWins ){
		auto q = quality( R"({"name":"x","quality":[{"status":"UncertainSubNormal","start":"PT2S","duration":"PT1S"}, {"status":"BadDeviceFailure","duration":"PT10S"}]})" );
		double value{};
		EXPECT_EQ( q.Apply(1s, 1, value), UA_STATUSCODE_BADDEVICEFAILURE );
		EXPECT_EQ( q.Apply(1s, 1, value), UA_STATUSCODE_UNCERTAINSUBNORMAL );
		EXPECT_EQ( q.Apply(1s, 1, value), UA_STATUSCODE_BADDEVICEFAILURE );
	}
	TEST( TagQualityTests, AReadingOutsideTheSensorRangeIsPinnedWithItsLimitBit ){
		auto q = quality( R"({"name":"x","mode":"sine","sensorMin":100,"sensorMax":200})" );
		double value{};
		EXPECT_EQ( q.Apply(1s, 150, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 150 );
		EXPECT_EQ( q.Apply(1s, 250, value), Compose(UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED, {.Limit=ELimit::High}) ); EXPECT_EQ( value, 200 );
		EXPECT_EQ( q.Apply(1s, 50, value), Compose(UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED, {.Limit=ELimit::Low}) ); EXPECT_EQ( value, 100 );
		EXPECT_EQ( q.Apply(1s, 200, value), UA_STATUSCODE_GOOD ); EXPECT_EQ( value, 200 );//at the range is inside it
	}
	TEST( TagQualityTests, AWindowTakesTheRangesLimitUnlessItNamesItsOwn ){
		auto inherits = quality( R"({"name":"x","mode":"sine","sensorMax":200,"quality":[{"status":"UncertainSensorNotAccurate","duration":"PT10S"}]})" );
		double value{};
		EXPECT_EQ( inherits.Apply(1s, 250, value), Compose(UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE, {.Limit=ELimit::High}) ); EXPECT_EQ( value, 200 );
		EXPECT_EQ( inherits.Apply(1s, 150, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE );
		auto own = quality( R"({"name":"x","mode":"sine","sensorMax":200,"quality":[{"status":"UncertainSensorNotAccurate","duration":"PT10S","limit":"none"}]})" );
		EXPECT_EQ( own.Apply(1s, 250, value), UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE ); EXPECT_EQ( value, 200 );
	}
	TEST( TagQualityTests, AHeldReadingKeepsTheLimitItWasTakenAt ){
		auto q = quality( R"({"name":"x","mode":"sine","sensorMax":200,"quality":[{"status":"BadSensorFailure","start":"PT2S","duration":"PT5S"}]})" );
		double value{};
		q.Apply( 1s, 250, value );
		EXPECT_EQ( q.Apply(1s, 150, value), Compose(UA_STATUSCODE_BADSENSORFAILURE, {.Limit=ELimit::High}) ); EXPECT_EQ( value, 200 );
	}
}

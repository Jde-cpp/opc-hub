//Signals:  TagSpec parsing - defaults, the refusals (#9 among them) - and the generators' shapes.
#include "../Signals.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	Ω spec( sv json )ε->TagSpec{ return TagSpec{ boost::json::parse(string{json}).as_object() }; }
	Ω gen( sv json )ε->up<IGenerator>{ return MakeGenerator( spec(json) ); }

	TEST( ParseModeTests, EveryNameRoundTripsAndNothingElseParses ){
		for( let name : {"sine", "ramp", "randomWalk", "counter", "toggle", "command", "follow", "production"} )
			EXPECT_EQ( ToString(ParseMode(name)), name );
		EXPECT_THROW( ParseMode("triangle"), Exception );
	}

	TEST( TagSpecTests, DefaultsAreTheDocumentedOnes ){
		let s = spec( R"({"name":"x"})" );
		EXPECT_EQ( s.Name, "x" );
		EXPECT_EQ( s.Mode, EMode::Counter );
		EXPECT_EQ( s.Min, 0 ); EXPECT_EQ( s.Max, 100 ); EXPECT_EQ( s.Step, 1 ); EXPECT_EQ( s.RatedRpm, 1450 );
		EXPECT_EQ( s.Period, Duration{30s} ); EXPECT_EQ( s.Tau, Duration{3s} );
		EXPECT_EQ( s.Cycle, Duration{1s} ); EXPECT_EQ( s.Jitter, Duration::zero() ); EXPECT_EQ( s.Reject, 0 ); EXPECT_EQ( s.RejectTag, "rejectCount" );
		EXPECT_FALSE( s.IsBool() );
	}
	TEST( TagSpecTests, OnlyToggleAndCommandAreBool ){
		EXPECT_TRUE( spec(R"({"name":"x","mode":"toggle"})").IsBool() );
		EXPECT_TRUE( spec(R"({"name":"x","mode":"command"})").IsBool() );
		EXPECT_FALSE( spec(R"({"name":"x","mode":"sine"})").IsBool() );
		EXPECT_FALSE( spec(R"({"name":"x","mode":"follow"})").IsBool() );
	}
	TEST( TagSpecTests, MaxMustExceedMinUnlessTheRangeIsUnused ){
		EXPECT_THROW( spec(R"({"name":"x","mode":"counter","min":10,"max":10})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"sine","min":10,"max":5})"), Exception );
		EXPECT_NO_THROW( spec(R"({"name":"x","mode":"toggle","min":10,"max":10})") );//a bool has no range
		EXPECT_NO_THROW( spec(R"({"name":"x","mode":"command","min":10,"max":10})") );
		EXPECT_NO_THROW( spec(R"({"name":"x","mode":"follow","min":10,"max":10})") );//follow tracks ratedRpm, not min/max
	}
	TEST( TagSpecTests, PeriodAndTauMustBePositive ){
		EXPECT_THROW( spec(R"({"name":"x","mode":"sine","period":"PT0S"})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"follow","tau":"PT0S"})"), Exception );
	}
	//#9:  randomWalk builds uniform_real_distribution{-step,step} (UB unless -step<=step) and counter never wraps on a
	//non-positive step - both must fail like the config typos they are;  a mode that never reads `step` is not policed.
	TEST( TagSpecTests, StepMustBePositiveWhereItIsUsed ){
		EXPECT_THROW( spec(R"({"name":"x","mode":"randomWalk","step":-10})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"randomWalk","step":0})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"counter","step":0})"), Exception );
		EXPECT_NO_THROW( spec(R"({"name":"x","mode":"sine","step":-1})") );
	}

	TEST( GeneratorTests, SineSpansMinToMaxOverAPeriod ){
		let g = gen( R"({"name":"s","mode":"sine","min":0,"max":100,"period":"PT8S"})" );
		EXPECT_NEAR( g->Next(2s, false), 100, 1e-9 );//a quarter period: the peak
		EXPECT_NEAR( g->Next(2s, false), 50, 1e-9 );//half: back through the middle
		EXPECT_NEAR( g->Next(2s, false), 0, 1e-9 );//three quarters: the trough
		EXPECT_NEAR( g->Next(2s, false), 50, 1e-9 );
	}
	TEST( GeneratorTests, RampIsASawtoothThatWraps ){
		let g = gen( R"({"name":"r","mode":"ramp","min":0,"max":100,"period":"PT10S"})" );
		EXPECT_NEAR( g->Next(2500ms, false), 25, 1e-9 );
		EXPECT_NEAR( g->Next(5s, false), 75, 1e-9 );
		EXPECT_NEAR( g->Next(5s, false), 25, 1e-9 );//12.5 s into a 10 s period
	}
	TEST( GeneratorTests, RandomWalkStaysInBoundsAndNeverJumpsMoreThanStep ){
		let g = gen( R"({"name":"w","mode":"randomWalk","min":40,"max":60,"step":5})" );
		double previous{ 50 };//starts at the middle
		for( uint i=0; i<2000; ++i ){
			let v = g->Next( 1s, false );
			ASSERT_GE( v, 40 ); ASSERT_LE( v, 60 );
			ASSERT_LE( std::abs(v-previous), 5+1e-9 );
			previous = v;
		}
	}
	TEST( GeneratorTests, CounterStepsAndWrapsToMin ){
		let g = gen( R"({"name":"c","mode":"counter","min":0,"max":3,"step":1})" );
		EXPECT_EQ( g->Next(1s, false), 1 );
		EXPECT_EQ( g->Next(1s, false), 2 );
		EXPECT_EQ( g->Next(1s, false), 3 );
		EXPECT_EQ( g->Next(1s, false), 0 );//4 > max wraps
		EXPECT_EQ( g->Next(1s, false), 1 );
	}
	TEST( GeneratorTests, ToggleStartsOnAndFlipsEveryPeriod ){
		let g = gen( R"({"name":"t","mode":"toggle","period":"PT2S"})" );
		EXPECT_EQ( g->Next(1s, false), 1 );
		EXPECT_EQ( g->Next(1s, false), 0 );//t reaches the period
		EXPECT_EQ( g->Next(2s, false), 1 );
	}
	TEST( GeneratorTests, FollowLagsTowardsRatedWhenCommandedAndTowardsZeroWhenNot ){
		let g = gen( R"({"name":"f","mode":"follow","ratedRpm":1450,"tau":"PT1S"})" );
		EXPECT_NEAR( g->Next(1s, true), 1450*(1-std::exp(-1.0)), 1e-6 );//916.6 - the emulator's first status line
		let second = g->Next( 1s, true );
		EXPECT_GT( second, 916 ); EXPECT_LT( second, 1450 );
		EXPECT_NEAR( g->Next(30s, true), 1450, 1e-6 );//settled
		EXPECT_NEAR( g->Next(1s, false), 1450*std::exp(-1.0), 1e-6 );//commanded off: decays
	}
	TEST( GeneratorTests, CommandTagsHaveNoGenerator ){
		EXPECT_THROW( gen(R"({"name":"status","mode":"command"})"), Exception );
	}

	TEST( TagSpecTests, ProductionRefusals ){
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","cycle":"PT0S"})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","cycle":"PT2S","jitter":"PT2S"})"), Exception );//a unit could take no time
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","reject":1.5})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","reject":-0.1})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","reject":0.1,"rejectTag":"x"})"), Exception );
		EXPECT_NO_THROW( spec(R"({"name":"x","mode":"production","min":10,"max":10,"cycle":"PT2S","jitter":"PT1.9S","reject":1})") );//no range
		EXPECT_THROW( MakeProduction(spec(R"({"name":"x","mode":"counter"})")), Exception );
	}
	TEST( TagSpecTests, AKeyOfTheWrongKindIsRefusedNotDefaulted ){//a typo has to fail like one
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","cycle":"2s"})"), Exception );//not ISO 8601:  read as the default, it counted twice as fast
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","jitter":"0.5"})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","reject":"0.05"})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"sine","period":30})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"follow","tau":"3s"})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","min":"1"})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":3})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","mode":"production","reject":0.1,"rejectTag":7})"), Exception );
		EXPECT_THROW( spec(R"({"name":"x","sensorMax":"1"})"), Exception );
	}
	TEST( ProductionTests, AUnitEveryCycle ){
		let g = gen( R"({"name":"totalCount","mode":"production","cycle":"PT2S"})" );
		EXPECT_EQ( g->Next(1s, true), 0 );
		EXPECT_EQ( g->Next(1s, true), 1 );
		EXPECT_EQ( g->Next(4s, true), 3 );//a period of two cycles counts both
		EXPECT_EQ( g->Next(1s, true), 3 );
		EXPECT_EQ( g->Next(1s, true), 4 );
	}
	TEST( ProductionTests, NothingAdvancesWhileStatusIsOff ){
		let g = gen( R"({"name":"totalCount","mode":"production","cycle":"PT2S"})" );
		EXPECT_EQ( g->Next(1s, true), 0 );
		EXPECT_EQ( g->Next(1h, false), 0 );
		EXPECT_EQ( g->Next(1s, true), 1 );//the unit in hand waited where it was
		EXPECT_EQ( g->Next(1h, false), 1 );
	}
	TEST( ProductionTests, JitterKeepsEachUnitWithinItsBounds ){
		let g = gen( R"({"name":"totalCount","mode":"production","cycle":"PT1S","jitter":"PT0.25S"})" );
		constexpr double step{ .01 };
		double count{}, sinceUnit{}, shortest{ 10 }, longest{};
		for( uint i=0; i<50'000; ++i ){//500 s
			sinceUnit += step;
			if( let next = g->Next(10ms, true); next!=count ){
				ASSERT_EQ( next, count+1 );
				count = next;
				shortest = std::min( shortest, sinceUnit ); longest = std::max( longest, sinceUnit );
				sinceUnit = 0;
			}
		}
		EXPECT_GE( shortest, .75-2*step ); EXPECT_LE( longest, 1.25+2*step );//a unit lands on the 10 ms sample after it ends, at both of its ends
		EXPECT_LT( shortest, .9 ); EXPECT_GT( longest, 1.1 );//and the jitter is there
		EXPECT_NEAR( count, 500, 25 );//+-.25 s uniform over 500 units:  sd 3.2
	}
	TEST( ProductionTests, RejectsAreAShareOfTheCount ){
		auto tenth = MakeProduction( spec(R"({"name":"totalCount","mode":"production","reject":0.1})") );
		EXPECT_EQ( tenth.Rejects->Next(1s, true), 0 );
		EXPECT_EQ( tenth.Count->Next(10'000s, true), 10'000 );
		let rejects = tenth.Rejects->Next( 1s, true );
		EXPECT_GT( rejects, 800 ); EXPECT_LT( rejects, 1200 );//sd 30
		EXPECT_EQ( tenth.Rejects->Next(1h, true), rejects );//the rejects' own sample counts nothing

		auto none = MakeProduction( spec(R"({"name":"totalCount","mode":"production"})") );
		none.Count->Next( 1000s, true );
		EXPECT_EQ( none.Rejects->Next(1s, true), 0 );
		auto all = MakeProduction( spec(R"({"name":"totalCount","mode":"production","reject":1})") );
		all.Count->Next( 1000s, true );
		EXPECT_EQ( all.Rejects->Next(1s, true), 1000 );
	}
}
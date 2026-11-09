//A line's combination (docs/oee/spec.md *Lines*):  its members' components summed, each factor over the machines that
//measure it.
#include "helpers.h"

namespace Jde::Opc::Oee::Tests{
	//A machine's bucket over eight planned hours, 06:00 to 14:00 with no calendar:  one stop from 06:00, and its counts.
	Ξ Member( Machine machine, Duration stopped, double total, double reject )ε->Bucket{
		vector<Interval> rows;
		for( auto t=At(6); t<At(14); t+=1h )
			rows.push_back( {t, 1h, 1h, {}, {}, total/8, (total-reject)/8, reject/8} );
		vector<Stop> stops;
		if( stopped>Duration::zero() )
			stops.push_back( {At(6), At(6)+stopped} );
		return Figures( machine, {rows, stops, {}, {}}, nullptr, At(6), At(14) )[0];
	}
	Ξ Combined( std::initializer_list<Bucket> members )ι->Bucket{ return Lines::Combine( At(6), At(14), "Day", std::span{members.begin(), members.size()} ); }

	struct LinesTests : ::testing::Test{
		//A filler at a second a unit and a packer at two, in bottles and in cases:  their counts do not add, their ideal times do.
		const Bucket Filler{ Member({1, "Filler", 1s, 5min, true, true}, 80min, 21600, 1080) };
		const Bucket Packer{ Member({2, "Packer", 2s, 5min, true, true}, 180min, 6000, 600) };
		const Bucket Labeller{ Member({3, "Labeller", nullopt, 5min, true, true}, {}, 1000, 100) };//no cycle.
		const Bucket Capper{ Member({4, "Capper", 1s, 5min, true, false}, 120min, 18000, 0) };//a Total alone:  no quality.
	};

	//At full coverage OEE is the good ideal time over the planned time:  the planned-time weighted mean of the machines'.
	TEST_F( LinesTests, WeightedMeanAtFullCoverage ){
		EXPECT_DOUBLE_EQ( *Filler.OEE, 342.0/480 );
		EXPECT_DOUBLE_EQ( *Packer.OEE, 180.0/480 );
		let line = Combined( {Filler, Packer} );
		EXPECT_EQ( line.Shift, "Day" );
		EXPECT_EQ( line.Planned, 960min );
		EXPECT_EQ( line.Run, 700min );
		EXPECT_EQ( line.Unplanned, 260min );
		EXPECT_EQ( line.Whole, 960min );
		EXPECT_EQ( line.Ideal, 560min );
		EXPECT_EQ( line.GoodIdeal, 522min );
		EXPECT_DOUBLE_EQ( *line.Availability, 700.0/960 );
		EXPECT_DOUBLE_EQ( *line.Performance, 560.0/700 );
		EXPECT_DOUBLE_EQ( *line.Quality, 522.0/560 );
		EXPECT_DOUBLE_EQ( *line.OEE, 522.0/960 );
		EXPECT_DOUBLE_EQ( *line.OEE, (480**Filler.OEE+480**Packer.OEE)/960 );
		EXPECT_DOUBLE_EQ( *line.TEEP, *line.OEE );
		EXPECT_EQ( line.Machines, 2u );
		EXPECT_EQ( line.PerformanceCoverage, 2u );
		EXPECT_EQ( line.QualityCoverage, 2u );
		EXPECT_EQ( line.Measured, EFactors::Availability|EFactors::Performance|EFactors::Quality );
		EXPECT_FALSE( line.Partial );
	}
	//A member line's bucket is its own:  an area of a line and a machine is the three machines'.
	TEST_F( LinesTests, Nested ){
		let nested = Combined( {Combined({Filler, Packer}), Capper} );
		let flat = Combined( {Filler, Packer, Capper} );
		EXPECT_EQ( nested.Machines, 3u );
		EXPECT_EQ( nested.Planned, flat.Planned );
		EXPECT_EQ( nested.Run, flat.Run );
		EXPECT_EQ( nested.Ideal, flat.Ideal );
		EXPECT_EQ( nested.PacedRun, flat.PacedRun );
		EXPECT_EQ( nested.GoodIdeal, flat.GoodIdeal );
		EXPECT_EQ( nested.QualityCoverage, flat.QualityCoverage );
		EXPECT_DOUBLE_EQ( *nested.Availability, *flat.Availability );
		EXPECT_DOUBLE_EQ( *nested.Performance, *flat.Performance );
		EXPECT_DOUBLE_EQ( *nested.Quality, *flat.Quality );
		EXPECT_DOUBLE_EQ( *nested.OEE, *flat.OEE );
		EXPECT_EQ( nested.Measured, flat.Measured );
	}
	//Performance is over the machines with a cycle, and so is quality, whose weights are ideal times.
	TEST_F( LinesTests, MemberWithoutACycle ){
		EXPECT_FALSE( Labeller.Performance );
		EXPECT_DOUBLE_EQ( *Labeller.Quality, 0.9 );
		let line = Combined( {Filler, Packer, Labeller} );
		EXPECT_EQ( line.Machines, 3u );
		EXPECT_EQ( line.PerformanceCoverage, 2u );
		EXPECT_EQ( line.QualityCoverage, 2u );
		EXPECT_DOUBLE_EQ( *line.Availability, 1180.0/1440 ) << "across every machine";
		EXPECT_DOUBLE_EQ( *line.Performance, 560.0/700 ) << "across the two with a cycle";
		EXPECT_DOUBLE_EQ( *line.Quality, 522.0/560 );
		EXPECT_DOUBLE_EQ( *line.OEE, (1180.0/1440)*(560.0/700)*(522.0/560) ) << "the product of the three as computed";
		EXPECT_EQ( line.Measured, EFactors::Availability ) << "what every machine measured";
	}
	TEST_F( LinesTests, MemberWithoutQuality ){
		EXPECT_FALSE( Capper.Quality );
		let line = Combined( {Filler, Capper} );
		EXPECT_EQ( line.PerformanceCoverage, 2u );
		EXPECT_EQ( line.QualityCoverage, 1u );
		EXPECT_DOUBLE_EQ( *line.Performance, (360.0+300)/(400+360) );
		EXPECT_DOUBLE_EQ( *line.Quality, 342.0/360 ) << "the filler's alone";
		EXPECT_EQ( line.Measured, EFactors::Availability|EFactors::Performance );
	}
	//`partial` is any member's, and a line of nothing measures nothing.
	TEST_F( LinesTests, PartialAndEmpty ){
		auto partial = Packer;
		partial.Partial = true;
		EXPECT_TRUE( Combined({Filler, partial}).Partial );
		let empty = Combined( {} );
		EXPECT_EQ( empty.Machines, 0u );
		EXPECT_FALSE( empty.Availability );
		EXPECT_FALSE( empty.OEE );
		EXPECT_EQ( empty.Measured, EFactors::None );
	}
}
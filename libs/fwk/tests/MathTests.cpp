#include <jde/fwk/utils/mathUtils.h>
#include <thread>

#define let const auto

namespace Jde::Tests{
	// fwk-max #7: the engine was default-constructed in debug builds, so every debug process replayed one fixed
	// sequence - and every thread in it opened on the same value.  It is now a per-thread absl::BitGen seeded from OS
	// entropy (mathUtils.h BitGen).  OpenSslTests.Random is no cover:  it exercises Crypto::Random (RAND_bytes).
	// The draws have to come from *fresh* threads: the main thread's engine may already have been advanced (TimerTests
	// draws from it for jitter), so only a thread's first value carries the signature.  The distinct count is the pin:
	// an unseeded, fixed-seeded or shared-seed engine opens every thread on one value.
	TEST( MathTests, RandomIsSeededPerThread ){
		constexpr uint threadCount{ 8 };
		vector<uint32> firstDraws( threadCount );
		{
			vector<std::jthread> threads;
			for( uint i=0; i<threadCount; ++i )
				threads.emplace_back( [&firstDraws, i]{ firstDraws[i] = Math::Random(); } );
		}
		//independently seeded engines: one pair repeating is 2^-32, so 8 draws collide about once in 1.5e8 runs.
		let distinct = flat_set<uint32>{ firstDraws.begin(), firstDraws.end() };
		EXPECT_EQ( distinct.size(), (uint)threadCount ) << "same first value on two threads - the engine is unseeded, fixed-seeded or not seeded per thread";
	}
	TEST( MathTests, RandomBounded ){
		flat_set<uint32> seen;
		for( uint i=0; i<1000; ++i ){
			let x = Math::Random( 5 );
			ASSERT_LT( x, 5u );
			seen.emplace( x );
		}
		EXPECT_EQ( seen.size(), 5u ) << "1000 draws missed a value of [0,5) - odds of 5*0.8^1000";
		EXPECT_LE( Math::Random(), (uint32)std::numeric_limits<uint32_t>::max() );
	}

	TEST( MathTests, StatisticsAllNegative ){
		let r = Math::Statistics( vector<double>{-3.0, -1.0, -2.0} );
		EXPECT_EQ( r.Max, -1.0 );
		EXPECT_EQ( r.Min, -3.0 );
		EXPECT_EQ( r.Average, -2.0 );
	}
}
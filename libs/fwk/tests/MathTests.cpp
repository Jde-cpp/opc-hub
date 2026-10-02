#include <jde/fwk/utils/mathUtils.h>
#include <thread>

#define let const auto

namespace Jde::Tests{
	// fwk-max #7: the engine was default-constructed in debug builds, so every debug process replayed one fixed
	// mt19937 sequence - and every thread in it opened on the same value.  fwk-review2 #6 closes that fix with
	// "covered by OpenSslTests.Random", but that test exercises Crypto::Random (RAND_bytes), a different generator;
	// reverting this one to `static thread_local std::mt19937 engine;` compiles and passes the whole suite.
	// The draws have to come from *fresh* threads: the main thread's engine may already have been advanced (TimerTests
	// draws from it for jitter), so only a thread's first value carries the signature.
	TEST( MathTests, RandomIsSeededPerThread ){
		constexpr uint32 unseededFirstDraw{ 3499211612u };//std::mt19937{}() - what an engine on the default seed opens with.
		constexpr uint threadCount{ 8 };
		vector<uint32> firstDraws( threadCount );
		{
			vector<std::jthread> threads;
			for( uint i=0; i<threadCount; ++i )
				threads.emplace_back( [&firstDraws, i]{ firstDraws[i] = Math::Random(); } );
		}
		for( uint i=0; i<threadCount; ++i )
			EXPECT_NE( firstDraws[i], unseededFirstDraw ) << "thread " << i << " opened on the default-seeded sequence - the engine is not being seeded";
		//independently seeded engines: one pair repeating is 2^-32, so 8 draws collide about once in 1.5e8 runs.
		let distinct = flat_set<uint32>{ firstDraws.begin(), firstDraws.end() };
		EXPECT_EQ( distinct.size(), (uint)threadCount ) << "two threads drew the same first value - they are not being seeded independently";
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

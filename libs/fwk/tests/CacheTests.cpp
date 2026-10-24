#include "jde/fwk/io/Cache.h"
#include "jde/fwk/chrono.h"
#include <jde/tests/WaitUntil.h>
#include <string>
#include <thread>

#define let const auto
namespace Jde::Tests{
	struct CacheTests : public ::testing::Test
	{};

	TEST_F( CacheTests, NegativeDuration ){
		Cache::Set<string>( "negative", "prior" );//negative-duration Set must also drop an existing entry, not just skip caching.
		auto p = Cache::Set<string>( "negative", "value", -1s );
		ASSERT_EQ( Cache::Get<string>("negative"), nullptr );
		ASSERT_TRUE( p );
		ASSERT_EQ( *p, "value" );
	}
	//the hour-long entry gives the sweep a pending timer to cancel, so the 1ms Set has to re-arm it earlier - alone or
	//shuffled first there is no timer yet, and only the start path runs.
	TEST_F( CacheTests, Expires ){
		Cache::Set<string>( "expires-long", "v", 1h );
		auto p = Cache::Set<string>( "expired", "value", 1ms );
		std::this_thread::sleep_for( 50ms );//expiry is sweep-driven; windows timers tick at ~15.6ms, so leave slack past the deadline.
		ASSERT_EQ( Cache::Get<string>("expired"), nullptr );
		EXPECT_TRUE( Cache::Clear("expires-long") );
	}
	// Internal::Set has to drop the old deadline before emplacing the new one, or the sweep evicts the freshly
	// assigned value at the *previous* entry's due time.  Nothing re-Set a live key with a longer duration, so
	// deleting that eraseTimeout left every existing test green.
	TEST_F( CacheTests, ReSetExtendsDeadline ){
		Cache::Set<string>( "reset", "a", 5ms );
		Cache::Set<string>( "reset", "b", 1h );
		std::this_thread::sleep_for( 60ms );//past the first deadline and a windows timer tick.
		let p = Cache::Get<string>( "reset" );
		ASSERT_NE( p, nullptr ) << "the stale 5ms deadline evicted the re-set value";
		EXPECT_EQ( *p, "b" );
		EXPECT_TRUE( Cache::Clear("reset") );
	}

	//no duration at all means no timeout row, so the sweep never sees the entry.  Clear's bool return - discarded
	//at every call site - is the only way to tell a hit from a miss.
	TEST_F( CacheTests, NoDurationNeverExpires ){
		Cache::Set<string>( "forever", "value", nullopt );
		std::this_thread::sleep_for( 60ms );
		let p = Cache::Get<string>( "forever" );
		ASSERT_NE( p, nullptr ) << "an entry with no duration must not be swept";
		EXPECT_EQ( *p, "value" );
		EXPECT_TRUE( Cache::Clear("forever") );
		EXPECT_FALSE( Cache::Clear("forever") ) << "the second clear erased nothing";
		EXPECT_EQ( Cache::Get<string>("forever"), nullptr );
	}

	//the zero-copy overload has no caller in the library either - FileAwait passes a string by value.
	TEST_F( CacheTests, SharedPointerOverload ){
		auto p = ms<const string>( "shared" );
		let cached = Cache::Set<string>( "sharedPtr", p );
		EXPECT_EQ( cached.get(), p.get() ) << "Set returns the caller's instance";
		let got = Cache::Get<string>( "sharedPtr" );
		ASSERT_NE( got, nullptr );
		EXPECT_EQ( got.get(), p.get() ) << "the cache shares the instance rather than copying it";
		EXPECT_TRUE( Cache::Clear("sharedPtr") );
	}

	//only the fallback half of Init: the suite config has no /cache section, and adding one to assert the
	//configured path would be a config change rather than a test.
	TEST_F( CacheTests, DefaultDurationFallsBackToAnHour ){
		EXPECT_EQ( Cache::DefaultDuration(), 1h );
	}

	//Get, Set and Clear from every core against five keys and the sweep, then every entry gone once its 10ms is up.  A
	//quarter second is ~18k sets and hundreds of sweep re-arms on a 32-core box:  fewer interleavings a run than a second's,
	//for a suite every build runs.
	TEST_F( CacheTests, Stress ){
		let threadCount = std::max( 2u, std::thread::hardware_concurrency() );
		let end = steady_clock::now()+250ms;
		vector<string> keys;
		for( uint i=0; i<5; ++i )
			keys.push_back( Ƒ("{}", i) );
		std::atomic<uint> retrieved{}, missed{}, sets{};
		{
			vector<std::jthread> workers;
			for( uint t=0; t<threadCount; ++t ){
				workers.emplace_back( [&, t]()ι->void {
					uint localRetrieved{}, localMissed{}, localSets{};
					for( uint i=t; steady_clock::now()<end; ++i ){
						let& key = keys[i%keys.size()];
						if( Cache::Get<string>(key) )
							++localRetrieved;
						else
							++localMissed;
						if( i%7==0 )
							Cache::Clear( key );
						else{
							Cache::Set<string>( key, ToIsoString(steady_clock::now()), i%3==0 ? 1ms : 10ms );
							++localSets;
						}
					}
					retrieved += localRetrieved; missed += localMissed; sets += localSets;
				} );
			}
		}//jthreads join here.
		INFOT( ELogTags::Test, "threads: {}, retrieved: {}, missed: {}, sets: {}", threadCount, retrieved.load(), missed.load(), sets.load() );
		let swept = [&keys]{ return std::ranges::all_of( keys, []( let& key ){ return !Cache::Get<string>(key); } ); };
		EXPECT_TRUE( WaitUntil(swept, 1s, 1ms) ) << "an entry outlived its 10ms - the sweep stopped re-arming";
	}
}
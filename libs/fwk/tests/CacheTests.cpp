#include <jde/fwk/io/Cache.h>
#include <jde/fwk/chrono.h>
#include <string>
#include <thread>

#define let const auto
namespace Jde::Tests{
	using StringCache = Cache<string,string>;//aliased: the comma in the template args would split the EXPECT_ macros' arguments.
	struct CacheTests : public ::testing::Test
	{};

	//Expiry is checked by Get, so an entry reads as a miss once its ttl is up with nothing having run in between.
	TEST_F( CacheTests, Expires ){
		StringCache cache{ 1h };
		cache.Set( "expired", "value", 1ms );
		cache.Set( "live", "value" );
		std::this_thread::sleep_for( 2ms );
		EXPECT_EQ( cache.Get("expired"), nullptr );
		EXPECT_NE( cache.Get("live"), nullptr ) << "a Set without a ttl takes the cache's default";
		EXPECT_TRUE( cache.Clear("expired") ) << "an expired entry stays until a Set drops it";
	}

	//a re-Set takes the new ttl:  the old deadline must not outlive it.
	TEST_F( CacheTests, ReSetExtendsDeadline ){
		StringCache cache{ 1h };
		cache.Set( "reset", "a", 1ms );
		cache.Set( "reset", "b", 1h );
		std::this_thread::sleep_for( 2ms );
		let p = cache.Get( "reset" );
		ASSERT_NE( p, nullptr ) << "the stale 1ms deadline expired the re-set value";
		EXPECT_EQ( *p, "b" );
	}

	TEST_F( CacheTests, ForeverNeverExpires ){
		StringCache cache{ 1ms };//a default shorter than the test, so only Forever keeps the entry.
		cache.Set( "forever", "value", StringCache::Forever );
		std::this_thread::sleep_for( 2ms );
		let p = cache.Get( "forever" );
		ASSERT_NE( p, nullptr ) << "an entry set Forever expired";
		EXPECT_EQ( *p, "value" );
		EXPECT_TRUE( cache.Clear("forever") );
		EXPECT_FALSE( cache.Clear("forever") ) << "the second clear erased nothing";
		EXPECT_EQ( cache.Get("forever"), nullptr );
	}

	TEST_F( CacheTests, SharedPointerOverload ){
		StringCache cache{ 1h };
		auto p = ms<const string>( "shared" );
		let cached = cache.Set( "sharedPtr", p );
		EXPECT_EQ( cached.get(), p.get() ) << "Set returns the caller's instance";
		let got = cache.Get( "sharedPtr" );
		ASSERT_NE( got, nullptr );
		EXPECT_EQ( got.get(), p.get() ) << "the cache shares the instance rather than copying it";
	}

	//only the fallback half of the default constructor:  the suite config has no /cache section, and adding one to assert
	//the configured path would be a config change rather than a test.
	TEST_F( CacheTests, DefaultTtlFallsBackToAnHour ){
		EXPECT_EQ( StringCache{}.DefaultTtl(), 1h );
	}

	//Set drops the expired entries once the map reaches 16, then twice what the drop left - never a Get, which only reads.
	TEST_F( CacheTests, SetDropsExpiredEntries ){
		StringCache cache{ 1h };
		for( uint i=0; i<16; ++i )
			cache.Set( Ƒ("expired-{}", i), "v", 0s );//past its deadline at once.
		EXPECT_EQ( cache.Get("expired-0"), nullptr );
		EXPECT_EQ( cache.Size(), 16u ) << "a Get dropped entries, or a Set below the threshold did";
		cache.Set( "live", "v" );
		EXPECT_EQ( cache.Size(), 1u ) << "the Set that reached the threshold kept the expired entries";
		EXPECT_NE( cache.Get("live"), nullptr );
	}

	//Get, Set and Clear from every core against five keys, then every entry gone once its 10ms is up.  A quarter second is
	//~18k sets on a 32-core box:  fewer interleavings a run than a second's, for a suite every build runs.
	TEST_F( CacheTests, Stress ){
		StringCache cache{ 1h };
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
						if( cache.Get(key) )
							++localRetrieved;
						else
							++localMissed;
						if( i%7==0 )
							cache.Clear( key );
						else{
							cache.Set( key, ToIsoString(steady_clock::now()), i%3==0 ? 1ms : 10ms );
							++localSets;
						}
					}
					retrieved += localRetrieved; missed += localMissed; sets += localSets;
				} );
			}
		}//jthreads join here.
		INFOT( ELogTags::Test, "threads: {}, retrieved: {}, missed: {}, sets: {}", threadCount, retrieved.load(), missed.load(), sets.load() );
		std::this_thread::sleep_for( 11ms );
		EXPECT_TRUE( std::ranges::all_of(keys, [&]( let& key ){ return !cache.Get(key); }) ) << "an entry outlived its 10ms";
	}
}
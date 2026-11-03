#include <thread>
#include <jde/fwk/io/Cache.h>
#include <jde/fwk/chrono.h>

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

	//Two reads of one key that finish out of order:  the read ticketed later stays cached, not the slower one.
	TEST_F( CacheTests, AnEarlierTicketKeepsALaterEntry ){
		StringCache cache{ 1h };
		let value = [&]( str key ){ let p = cache.Get( key ); return p ? *p : string{ "<miss>" }; };
		let earlier = cache.Ticket();
		let later = cache.Ticket();
		cache.Set( "key", "later", {}, later );
		cache.Set( "key", "earlier", {}, earlier );
		EXPECT_EQ( value("key"), "later" ) << "the slower read replaced the newer one";

		cache.Set( "key", "plain" );
		EXPECT_EQ( value("key"), "plain" ) << "a plain Set counts as a read taken at the Set";
		cache.Set( "key", "late", {}, later );
		EXPECT_EQ( value("key"), "plain" ) << "a read ticketed before the plain Set replaced it";

		cache.Set( "expired", "later", 0s, later );
		cache.Set( "expired", "earlier", {}, earlier );
		EXPECT_EQ( value("expired"), "earlier" ) << "an expired entry held off a live value";
	}

	//A read ticketed before a Clear that finishes after it:  the key stays a miss.
	TEST_F( CacheTests, AClearKeepsOutAnEarlierRead ){
		StringCache cache{ 1h };
		cache.Set( "key", "old" );
		let beforeClear = cache.Ticket();
		EXPECT_TRUE( cache.Clear("key") );
		cache.Set( "key", "stale", {}, beforeClear );
		EXPECT_EQ( cache.Get("key"), nullptr ) << "the read that started before the Clear landed after it";
		EXPECT_FALSE( cache.Clear("key") ) << "a cleared key holds no value";

		let missBeforeClear = cache.Ticket();
		EXPECT_FALSE( cache.Clear("never-set") );
		cache.Set( "never-set", "stale", {}, missBeforeClear );
		EXPECT_EQ( cache.Get("never-set"), nullptr ) << "a Clear of a key with no entry let an earlier read in";

		cache.Set( "key", "fresh", {}, cache.Ticket() );
		EXPECT_NE( cache.Get("key"), nullptr ) << "a read ticketed after the Clear was kept out";
	}

	//only the fallback half of the default constructor:  the suite config has no /cache section, and adding one to assert
	//the configured path would be a config change rather than a test.
	TEST_F( CacheTests, DefaultTtlFallsBackToAnHour ){
		EXPECT_EQ( StringCache{}.DefaultTtl(), 1h );
	}

	//Set drops the expired entries once the map reaches 16, then twice what the drop left - never a Get, which only reads.
	TEST_F( CacheTests, SetDropsExpiredEntries ){
		StringCache cache{ 1h };
		cache.Set( "kept", "v" );
		for( uint i=0; i<15; ++i )
			cache.Set( Ƒ("expired-{}", i), "v", 0s );//past its deadline at once.
		EXPECT_EQ( cache.Get("expired-0"), nullptr );
		EXPECT_EQ( cache.Size(), 16u ) << "a Get dropped entries, or a Set below the threshold did";
		cache.Set( "live", "v" );
		EXPECT_EQ( cache.Size(), 2u ) << "the Set that reached the threshold kept the expired entries";
		EXPECT_NE( cache.Get("kept"), nullptr ) << "the drop took a live entry";
		EXPECT_NE( cache.Get("live"), nullptr );
	}

	//Get, Set and Clear from every core against five keys, beside a churn of keys each set once for 1ms.  Only a drop removes
	//those, so the map keeps reaching the drop threshold and Set drops while the other threads read and write.  Then every
	//entry is gone once its 10ms is up, and the drops kept the map near what was live.  A quarter second keeps the
	//interleavings to fewer than a second's would, for a suite every build runs.
	TEST_F( CacheTests, Stress ){
		StringCache cache{ 1h };
		let threadCount = std::max( 2u, std::thread::hardware_concurrency() );
		let end = steady_clock::now()+250ms;
		vector<string> keys;
		for( uint i=0; i<5; ++i )
			keys.push_back( Ƒ("{}", i) );
		std::atomic<uint> retrieved{}, missed{}, sets{}, churned{};
		{
			vector<std::jthread> workers;
			for( uint t=0; t<threadCount; ++t ){
				workers.emplace_back( [&, t]()ι->void {
					uint localRetrieved{}, localMissed{}, localSets{}, localChurned{};
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
						if( i%2==0 ){
							cache.Set( Ƒ("{}-{}", t, i), "churn", 1ms );
							++localChurned;
						}
					}
					retrieved += localRetrieved; missed += localMissed; sets += localSets; churned += localChurned;
				} );
			}
		}//jthreads join here.
		INFOT( ELogTags::Test, "threads: {}, retrieved: {}, missed: {}, sets: {}, churned: {}, size: {}", threadCount, retrieved.load(), missed.load(), sets.load(), churned.load(), cache.Size() );
		std::this_thread::sleep_for( 11ms );
		EXPECT_TRUE( std::ranges::all_of(keys, [&]( let& key ){ return !cache.Get(key); }) ) << "an entry outlived its 10ms";
		EXPECT_LT( cache.Size(), churned.load()/2 ) << "Set stopped dropping the expired entries";
	}
}
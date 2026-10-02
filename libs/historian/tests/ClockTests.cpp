//The injected clock:  ManualClock runs midnight, delay and heartbeats without waiting, and the day helpers put midnight
//where an IANA zone does, across both DST changes.  SystemClock is the one test here that waits, briefly.
#include <latch>
#include <thread>
#include <jde/historian/Clock.h>
#include "ManualClock.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	constexpr TimePoint _start{ sys_days{2026y/March/7}+17h };

	TEST( ClockTests, AdvanceRunsDueTimersInOrder ){
		ManualClock clock{ _start };
		vector<tuple<int,TimePoint>> ran;
		clock.Schedule( _start+2min, [&]{ ran.emplace_back( 2, clock.Now() ); } );
		clock.Schedule( _start+1min, [&]{ ran.emplace_back( 1, clock.Now() ); } );
		clock.Schedule( _start+2min, [&]{ ran.emplace_back( 3, clock.Now() ); } );//ties run in the order scheduled.
		clock.Schedule( _start+5min, [&]{ ran.emplace_back( 5, clock.Now() ); } );

		EXPECT_EQ( clock.Advance(2min), 3 );
		ASSERT_EQ( ran.size(), 3 );
		EXPECT_EQ( ran[0], make_tuple(1, _start+1min) );//Now() is each timer's own due time.
		EXPECT_EQ( ran[1], make_tuple(2, _start+2min) );
		EXPECT_EQ( ran[2], make_tuple(3, _start+2min) );
		EXPECT_EQ( clock.Now(), _start+2min );
		EXPECT_EQ( clock.Pending(), 1 );
	}

	//A heartbeat timer re-arms itself from its own callback; one Advance runs every beat that falls inside it.
	TEST( ClockTests, CallbacksRescheduleWithinAnAdvance ){
		ManualClock clock{ _start };
		vector<TimePoint> beats;
		std::function<void()> beat = [&]{
			beats.push_back( clock.Now() );
			clock.Schedule( clock.Now()+10s, [&]{ beat(); } );
		};
		clock.Schedule( _start+10s, [&]{ beat(); } );
		EXPECT_EQ( clock.Advance(1min), 6 );
		ASSERT_EQ( beats.size(), 6 );
		EXPECT_EQ( beats.back(), _start+1min );
		EXPECT_EQ( clock.Pending(), 1 );
	}

	TEST( ClockTests, Cancel ){
		ManualClock clock{ _start };
		bool fired{};
		let id = clock.Schedule( _start+1min, [&]{ fired = true; } );
		let other = clock.Schedule( _start, [&]{} );
		EXPECT_TRUE( clock.Cancel(id) );
		EXPECT_FALSE( clock.Cancel(id) );
		EXPECT_EQ( clock.Advance(1h), 1 );
		EXPECT_FALSE( fired );
		EXPECT_FALSE( clock.Cancel(other) );//already ran.
	}

	TEST( ClockTests, SystemClock ){
		auto clock = SystemClock();
		std::latch done{ 1 };
		std::atomic<bool> cancelledRan{};
		let cancelled = clock->Schedule( clock->Now()+20ms, [&]{ cancelledRan = true; } );
		clock->Schedule( clock->Now()+50ms, [&]{ done.count_down(); } );
		EXPECT_TRUE( clock->Cancel(cancelled) );
		let start = steady_clock::now();
		while( !done.try_wait() && steady_clock::now()-start<10s )
			std::this_thread::sleep_for( 1ms );
		EXPECT_TRUE( done.try_wait() );
		EXPECT_FALSE( cancelledRan );
	}

	Ω local( TimePoint t, const time_zone& tz )ι->local_seconds{ return floor<seconds>( tz.to_local(t) ); }

	//New York: spring forward and fall back at 02:00, so midnight always exists and only the days' lengths change.
	TEST( ClockTests, MidnightAcrossDST ){
		let& tz = *locate_zone( "America/New_York" );
		ManualClock clock{ _start };//2026-03-07 12:00 EST.
		vector<TimePoint> midnights;
		std::function<void()> midnight = [&]{
			midnights.push_back( clock.Now() );
			clock.Schedule( NextDayStart(clock.Now(), tz), [&]{ midnight(); } );
		};
		clock.Schedule( NextDayStart(clock.Now(), tz), [&]{ midnight(); } );
		clock.Advance( days{3} );
		ASSERT_EQ( midnights.size(), 3 );
		EXPECT_EQ( midnights[0], sys_days{2026y/March/8}+5h );//00:00 EST.
		EXPECT_EQ( midnights[1], sys_days{2026y/March/9}+4h );//00:00 EDT:  a 23-hour day.
		EXPECT_EQ( midnights[2], sys_days{2026y/March/10}+4h );
		for( let& m : midnights )
			EXPECT_EQ( local(m, tz).time_since_epoch()%days{1}, seconds::zero() );

		let fallBack = DayStart( 2026y/November/1, tz );
		EXPECT_EQ( fallBack, sys_days{2026y/November/1}+4h );
		EXPECT_EQ( DayStart(2026y/November/2, tz)-fallBack, 25h );
	}

	//Havana changes at midnight itself:  in March 00:00 never happens, and in November it happens twice.
	TEST( ClockTests, MidnightSkippedAndRepeated ){
		let& tz = *locate_zone( "America/Havana" );
		let skipped = DayStart( 2026y/March/8, tz );
		EXPECT_EQ( skipped, sys_days{2026y/March/8}+5h );//the change itself, 01:00 CDT.
		EXPECT_EQ( local(skipped, tz), local_days{2026y/March/8}+1h );
		EXPECT_EQ( DayOf(skipped, tz), 2026y/March/8 );
		EXPECT_EQ( DayOf(skipped-1s, tz), 2026y/March/7 );
		EXPECT_EQ( DayStart(2026y/March/9, tz)-skipped, 23h );

		let repeated = DayStart( 2026y/November/1, tz );
		EXPECT_EQ( repeated, sys_days{2026y/November/1}+4h );//the first 00:00, CDT.
		EXPECT_EQ( DayOf(repeated-1s, tz), 2026y/October/31 );
		EXPECT_EQ( DayOf(repeated+90min, tz), 2026y/November/1 );//00:30 CST, the second time round.
		EXPECT_EQ( DayStart(2026y/November/2, tz)-repeated, 25h );
		EXPECT_EQ( NextDayStart(repeated-1s, tz), repeated );
	}
}
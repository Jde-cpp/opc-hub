#include <semaphore>
#include <thread>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/utils/mathUtils.h>
#ifdef _WIN32
	#include <windows.h>
	#include <timeapi.h>          // timeBeginPeriod/timeEndPeriod (WIN32_LEAN_AND_MEAN excludes it from windows.h)
	#pragma comment( lib, "winmm.lib" )
#endif

#define let const auto
namespace Jde::Tests{
	using std::chrono::microseconds;
	constexpr ELogTags _tags{ ELogTags::Test };
	struct TimerTests : public ::testing::Test
	{};

	// Windows' default timer/scheduler resolution is ~15.6ms, so every sub-millisecond timer/sleep in the
	// loop below rounds up to a full tick — an iteration then costs ~15ms instead of ~0.1ms. Raise the
	// resolution to 1ms for the test's duration (no-op on Linux, which already honors µs-scale delays).
	struct TimerResolutionGuard{
#ifdef _WIN32
		TimerResolutionGuard()ι{ timeBeginPeriod( 1 ); }
		~TimerResolutionGuard(){ timeEndPeriod( 1 ); }
#endif
	};

	//the test's own state, co-owned with the coroutine: as namespace-scope variables the counters were never
	//reset, so every --gtest_repeat iteration after the first summed with the previous ones and failed.
	struct Outcomes{
		std::atomic<uint> Success{};
		std::atomic<uint> Canceled{};
		std::atomic<uint> Early{};//fired before its delay elapsed - a timer must never do that.
		std::atomic<uint> WrongError{};//canceled with something other than operation_aborted.
	};
	Ω test( sp<std::binary_semaphore> done, uint i, sp<Outcomes> outcomes )ι->VoidTask{
		let delay = Math::Random( _windows ? 4000 : 200 );
		let kill = Math::Random( _windows ? 4000 : 100 );
		atomic_flag threadDone;
		atomic_flag threadStart;
		DurationTimer timer{ microseconds(delay) };
		std::jthread t{ [cancel=timer.Canceller(), kill, &threadDone, &threadStart]()ι->void {
			threadStart.test_and_set();
			threadStart.notify_all();
			std::this_thread::sleep_for( microseconds(kill) );
			cancel.Cancel();
			threadDone.test_and_set();
			threadDone.notify_all();
		} };
		threadStart.wait( false );
		let start = steady_clock::now();//before the co_await, where Suspend arms expires_after, so a fired timer can never resume earlier than start+delay.
		auto result = co_await timer;
		let elapsed = duration_cast<microseconds>( steady_clock::now()-start );
		let early = result.has_value() && elapsed<microseconds(delay);
		let wrongError = !result.has_value() && result.error()!=boost::asio::error::operation_aborted;//any error_code passed before - a timer that failed for another reason counted as a cancel.
		if( early || wrongError )//only the timer a tally fails on, so a failed tally still names it.
			TRACE( "[{}]completed: {}, delay: {}, kill: {}, time: {}", hex(i), result.has_value() ? "true" : "canceled", delay, kill, elapsed.count() );
		if( result.has_value() ){
			++outcomes->Success;
			if( early )//tally rather than EXPECT: this resumes on an executor thread and GoogleTest assertions are main-thread only.
				++outcomes->Early;
		}
		else{
			++outcomes->Canceled;
			if( wrongError )
				++outcomes->WrongError;
		}

		threadDone.wait( false );
		done->release();
	}
	Ω negativeTest( atomic_flag& done, std::expected<void, boost::system::error_code>& result, string& error )ι->VoidTask{
		try{
			result = co_await DurationTimer{ microseconds(-1) };
		}
		catch( const std::exception& e ){
			error = e.what();
		}
		done.test_and_set();
		done.notify_all();
	}
	// Regression: a negative duration is ready immediately and never suspends - await_resume must complete
	// successfully instead of throwing for a missing result.
	TEST_F( TimerTests, NegativeDuration ){
		atomic_flag done;
		std::expected<void, boost::system::error_code> result{ std::unexpected{boost::system::error_code{}} };
		string error;
		negativeTest( done, result, error );
		done.wait( false );
		ASSERT_TRUE( error.empty() ) << error;
		ASSERT_TRUE( result.has_value() );
	}

	TEST_F( TimerTests, General ){
#ifdef _WIN32
		TimerResolutionGuard timerResolution;
#endif
		constexpr uint testCount = _windows ? 1024 : 2048;//a stress of Cancel racing Suspend as well as the tallies:  enough that both outcomes stay plentiful on a loaded runner.
		auto outcomes = ms<Outcomes>();
		for( uint i=0; i<testCount; ++i ){
			auto done = ms<std::binary_semaphore>( 0 );
			test( done, i, outcomes );
			//deadline, not an untimed wait: a throw inside the coroutine never signals, and that has to fail the run rather than hang it - addJdeTest's ctest TIMEOUT would kill the whole suite with nothing said about which timer.
			ASSERT_TRUE( done->try_acquire_for(10s) ) << "timer " << i << " never completed";
		}
		TRACE( "success: {}, canceled: {}", outcomes->Success.load(), outcomes->Canceled.load() );
		EXPECT_EQ( outcomes->Early.load(), 0u ) << "a timer resumed before its delay elapsed";
		EXPECT_EQ( outcomes->WrongError.load(), 0u ) << "a canceled timer reported something other than operation_aborted";
		EXPECT_GT( outcomes->Success.load(), 0u ) << "every timer was canceled - the fired path never ran";
		EXPECT_GT( outcomes->Canceled.load(), 0u ) << "no timer was ever canceled - the unexpected path never ran";
		ASSERT_EQ( outcomes->Success+outcomes->Canceled, testCount );
	}

	//C10: Cancel() before anything awaits the timer found no pending async_wait, so it cancelled nothing and the wait
	//then ran its full duration.  The live case is RemoteLog's destructor, which cancels in the window between
	//RemoteLog::Run assigning _timer and the co_await reaching Suspend->Start - it then blocked for one _delay, and
	//under a stopped io_context for the watchdog instead.  Cancel is remembered now and Suspend re-issues it.
	Ω cancelBeforeStartTest( sp<std::binary_semaphore> done, DurationTimer timer, sp<std::atomic<bool>> aborted )ι->VoidTask{
		let result = co_await timer;
		*aborted = !result.has_value() && result.error()==boost::asio::error::operation_aborted;//tallied, not EXPECTed: this is an executor thread.
		done->release();
	}
	TEST_F( TimerTests, CancelBeforeStart ){
		DurationTimer timer{ std::chrono::seconds(10) };//far longer than the deadline below, so only the cancel can meet it.
		timer.Cancel();//no async_wait exists yet - this is the whole point.
		auto done = ms<std::binary_semaphore>( 0 );
		auto aborted = ms<std::atomic<bool>>();
		cancelBeforeStartTest( done, move(timer), aborted );
		ASSERT_TRUE( done->try_acquire_for(5s) ) << "a cancel that arrived before the wait was started was lost - the timer is running out its full duration";
		EXPECT_TRUE( aborted->load() ) << "it completed, but not with operation_aborted";
	}

	// The second ctor builds the timer on a caller-supplied executor.  Its only caller repo-wide is
	// OpcGateway's AsyncRequest, which relies on resuming back on its strand - a regression that resumed on an
	// arbitrary pool thread would surface there as a data race rather than as a failure, and nothing constructed
	// this overload in any test.
	using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;
	Ω strandTest( sp<std::binary_semaphore> done, Strand strand, sp<std::atomic<bool>> onStrand, sp<std::atomic<bool>> fired )ι->VoidTask{
		*fired = (co_await DurationTimer{ microseconds(100), strand }).has_value();
		*onStrand = strand.running_in_this_thread();//tallied, not EXPECTed: this is an executor thread.
		done->release();
	}
	TEST_F( TimerTests, ResumesOnStrand ){
		auto strand = boost::asio::make_strand( *Executor() );
		auto done = ms<std::binary_semaphore>( 0 );
		auto onStrand = ms<std::atomic<bool>>(), fired = ms<std::atomic<bool>>();
		strandTest( done, strand, onStrand, fired );
		ASSERT_TRUE( done->try_acquire_for(10s) ) << "the strand-bound timer never resumed";
		EXPECT_TRUE( fired->load() ) << "the strand-bound timer completed with an error";
		EXPECT_TRUE( onStrand->load() ) << "the completion resumed off the strand it was bound to";
	}

	//A timer awaited again, as AsyncRequest's poll sleep is:  each Rearm waits its own duration, and a Cancel - which a sleep cut short
	//by a request leaves set - ends only the wait it was meant for, not the next.
	Ω rearmTest( sp<std::binary_semaphore> done, sp<std::array<std::atomic<bool>,3>> outcomes )ι->VoidTask{
		DurationTimer timer{ std::chrono::seconds(10) };
		timer.Cancel();
		let cancelled = co_await timer;
		(*outcomes)[0] = !cancelled.has_value() && cancelled.error()==boost::asio::error::operation_aborted;
		let start = steady_clock::now();
		let rearmed = co_await timer.Rearm( 2ms );
		(*outcomes)[1] = rearmed.has_value() && steady_clock::now()-start>=2ms;
		(*outcomes)[2] = (co_await timer.Rearm( 1ms )).has_value();
		done->release();
	}
	TEST_F( TimerTests, Rearm ){
		auto done = ms<std::binary_semaphore>( 0 );
		auto outcomes = ms<std::array<std::atomic<bool>,3>>();
		rearmTest( done, outcomes );
		ASSERT_TRUE( done->try_acquire_for(5s) ) << "the timer never resumed";
		EXPECT_TRUE( (*outcomes)[0].load() ) << "the first wait was not cancelled";
		EXPECT_TRUE( (*outcomes)[1].load() ) << "the rearmed wait failed - the earlier cancel stuck - or ended early";
		EXPECT_TRUE( (*outcomes)[2].load() ) << "the second rearm failed";
	}

	//A canceller shares the timer's state, not the awaitable:  holders such as IClientSocketSession::_timeouts cancel after the
	//awaiting frame - and the DurationTimer in it - is gone.
	Ω cancelAfterTest( sp<std::binary_semaphore> done, sp<TimerCancel> cancel, sp<std::atomic<bool>> fired )ι->VoidTask{
		DurationTimer timer{ microseconds(100) };
		*cancel = timer.Canceller();
		*fired = (co_await timer).has_value();
		done->release();
	}
	TEST_F( TimerTests, CancelOutlivesTimer ){
		auto done = ms<std::binary_semaphore>( 0 );
		auto cancel = ms<TimerCancel>();
		auto fired = ms<std::atomic<bool>>();
		cancelAfterTest( done, cancel, fired );
		ASSERT_TRUE( done->try_acquire_for(10s) ) << "the timer never resumed";
		EXPECT_TRUE( fired->load() ) << "the timer completed with an error";
		std::this_thread::sleep_for( 10ms );//let the frame, and the DurationTimer in it, be destroyed.
		cancel->Cancel();//clean under ASan:  a no-op on the state the canceller still holds.
	}
}
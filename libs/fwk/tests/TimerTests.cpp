#include <semaphore>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/fwk/utils/mathUtils.h>
#include <thread>
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
	// loop below rounds up to a full tick — 8192 sequential iterations then take ~123s instead of ~1s. Raise
	// the resolution to 1ms for the test's duration (no-op on Linux, which already honors µs-scale delays).
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
	Ω test( sp<std::binary_semaphore> done, uint i, sp<Outcomes> outcomes )ι->TimerAwait::Task{
		let delay = Math::Random( _windows ? 4000 : 200 );
		let kill = Math::Random( _windows ? 4000 : 100 );
		atomic_flag threadDone;
		atomic_flag threadStart;
		auto timer = mu<DurationTimer>( microseconds(delay) );
		std::jthread t{ [&timer, kill, &threadDone, &threadStart]()ι->void {
			threadStart.test_and_set();
			threadStart.notify_all();
			std::this_thread::sleep_for( microseconds(kill) );
			timer->Cancel();
			threadDone.test_and_set();
			threadDone.notify_all();
		} };
		threadStart.wait( false );
		let start = steady_clock::now();//before the co_await, where Suspend->Start arms expires_after, so a fired timer can never resume earlier than start+delay.
		auto result = co_await *timer;
		let elapsed = duration_cast<microseconds>( steady_clock::now()-start );
		TRACE( "[{}]completed: {}, delay: {}, kill: {}, time: {}", hex(i), result.has_value() ? "true" : "canceled", delay, kill, elapsed.count() );
		if( result.has_value() ){
			++outcomes->Success;
			if( elapsed<microseconds(delay) )//tally rather than EXPECT: this resumes on an executor thread and GoogleTest assertions are main-thread only.
				++outcomes->Early;
		}
		else{
			++outcomes->Canceled;
			if( result.error()!=boost::asio::error::operation_aborted )//any error_code passed before - a timer that failed for another reason counted as a cancel.
				++outcomes->WrongError;
		}

		threadDone.wait( false );
		done->release();
	}
	Ω negativeTest( atomic_flag& done, std::expected<void, boost::system::error_code>& result, string& error )ι->TimerAwait::Task{
		try{
			result = co_await DurationTimer{ microseconds(-1) };
		}
		catch( const std::exception& e ){
			error = e.what();
		}
		done.test_and_set();
		done.notify_all();
	}
	// Regression: a negative duration is ready immediately, so no promise is attached - await_resume
	// must complete successfully instead of throwing 'promise is null'.
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
		constexpr uint testCount = _windows ? 1024 : 4096*2;
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
	//under a stopped io_context for the watchdog instead.  Cancel is remembered now and Start re-issues it.
	Ω cancelBeforeStartTest( sp<std::binary_semaphore> done, sp<DurationTimer> timer, sp<std::atomic<bool>> aborted )ι->TimerAwait::Task{
		let result = co_await *timer;
		*aborted = !result.has_value() && result.error()==boost::asio::error::operation_aborted;//tallied, not EXPECTed: this is an executor thread.
		done->release();
	}
	TEST_F( TimerTests, CancelBeforeStart ){
		auto timer = ms<DurationTimer>( std::chrono::seconds(10) );//far longer than the deadline below, so only the cancel can meet it.
		timer->Cancel();//no async_wait exists yet - this is the whole point.
		auto done = ms<std::binary_semaphore>( 0 );
		auto aborted = ms<std::atomic<bool>>();
		cancelBeforeStartTest( done, timer, aborted );
		ASSERT_TRUE( done->try_acquire_for(5s) ) << "a cancel that arrived before the wait was started was lost - the timer is running out its full duration";
		EXPECT_TRUE( aborted->load() ) << "it completed, but not with operation_aborted";
	}

	// The second ctor binds the completion handler to a caller-supplied executor.  Its only caller repo-wide is
	// OpcGateway's AsyncRequest, which relies on resuming back on its strand - a regression that resumed on an
	// arbitrary pool thread would surface there as a data race rather than as a failure, and nothing constructed
	// this overload in any test.
	using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;
	Ω strandTest( sp<std::binary_semaphore> done, Strand strand, sp<std::atomic<bool>> onStrand, sp<std::atomic<bool>> fired )ι->TimerAwait::Task{
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
}
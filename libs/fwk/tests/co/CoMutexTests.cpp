#include <jde/fwk/co/CoMutex.h>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/process/execution.h>
#include <atomic>
#include <thread>

#define let const auto

namespace Jde::Tests{
	struct CoMutexTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()ι->void{
			Execution::Run();
		}
	};

	//deliberately not named waitFor: ExecutionTests.cpp already has a Jde::Tests::waitFor with a different deadline,
	//and two differing definitions of one template in one namespace is an ODR violation nothing would report.
	Ŧ untilTimeout( T predicate )ι->bool{
		let deadline = steady_clock::now()+15s;
		while( !predicate() && steady_clock::now()<deadline )
			std::this_thread::sleep_for( 1ms );
		return predicate();
	}

	//TryLock: for callers that cannot wait on a holder that may never resume - e.g. ProtoLog::Shutdown, which runs after
	//the executor has been destroyed.  A failed attempt takes and queues nothing.
	TEST_F( CoMutexTests, TryLockIsExclusive ){
		CoMutex m;
		auto first = m.TryLock();
		ASSERT_TRUE( first );
		for( uint i=0; i<8; ++i )
			EXPECT_FALSE( m.TryLock() ) << "the lock was handed out twice";
		first.unlock();
		EXPECT_FALSE( first ) << "an unlocked guard still claims the lock";
		EXPECT_TRUE( m.TryLock() ) << "released, so it must be acquirable again";
	}

	//nothing happens until the awaiter suspends:  one that is made and dropped holds no place in the queue.
	TEST_F( CoMutexTests, AnUnawaitedLockTakesNothing ){
		CoMutex m;
		m.Lock();
		EXPECT_TRUE( m.TryLock() );
	}

	Ω lockOnce( CoMutex& m, bool& acquired )->VoidAwait::Task{
		auto guard = co_await m.Lock();
		acquired = true;
	}
	//uncontended, await_suspend acquires and declines to suspend - no trip through the executor.
	TEST_F( CoMutexTests, AnUncontendedLockDoesNotSuspend ){
		CoMutex m;
		bool acquired{};
		lockOnce( m, acquired );
		EXPECT_TRUE( acquired ) << "the coroutine suspended on a free lock";
		EXPECT_TRUE( m.TryLock() ) << "the coroutine's guard did not release";
	}

	Ω hold( CoMutex& m, std::atomic<uint>& holders, std::atomic<bool>& overlapped, std::atomic<uint>& completed )->VoidAwait::Task{
		{
			auto guard = co_await m.Lock();
			if( ++holders!=1 )
				overlapped = true;
			std::this_thread::sleep_for( 100us );//hold long enough for other threads to queue behind us.
			--holders;
		}//release before ++completed, so the test cannot tear down `m` mid-Unlock.
		++completed;
	}
	//Three ways to break it all land here:  two holders at once, an Unlock that does not Post the next waiter, and a
	//waiter lost from the queue.
	TEST_F( CoMutexTests, MutualExclusion ){
		CoMutex m;
		std::atomic<uint> holders{}; std::atomic<bool> overlapped{}; std::atomic<uint> completed{};
		constexpr uint total{ 64 };
		{
			vector<std::jthread> threads;
			for( uint i=0; i<total; ++i )
				threads.emplace_back( [&](){ hold(m, holders, overlapped, completed); } );
		}//launchers join here; suspended coroutines resume on the executor.
		ASSERT_TRUE( untilTimeout([&]{ return completed.load()==total; }) ) << "waiter chain stalled at " << completed.load() << " of " << total << " - a lost wakeup in Unlock";
		ASSERT_FALSE( overlapped.load() ) << "two coroutines held the lock simultaneously";
		EXPECT_TRUE( m.TryLock() ) << "still held after all 64 holders released";
	}

	Ω acquire( CoMutex& m, sp<CoGuard> out, sp<std::atomic<bool>> done )->VoidAwait::Task{
		*out = co_await m.Lock();
		*done = true;
	}
	//Unlock hands the lock to the waiter before the waiter runs, so a TryLock in between fails whether or not it has run yet.
	TEST_F( CoMutexTests, UnlockHandsOverWithoutBarging ){
		CoMutex m;
		auto held = m.TryLock();
		ASSERT_TRUE( held );
		auto waiterGuard = ms<CoGuard>(); auto done = ms<std::atomic<bool>>();
		acquire( m, waiterGuard, done );
		EXPECT_FALSE( done->load() ) << "acquired a held lock";
		held.unlock();
		EXPECT_FALSE( m.TryLock() ) << "barged in ahead of the waiter it was handed to";
		ASSERT_TRUE( untilTimeout([&]{ return done->load(); }) ) << "the waiter was never resumed";
		waiterGuard->unlock();
		EXPECT_TRUE( m.TryLock() );
	}

	//separate parameters so -Wself-move cannot see the two expressions as identical.
	Ω assignFrom( CoGuard& lhs, CoGuard& rhs )ι->void{ lhs = move(rhs); }

	//operator= releases what it is overwriting; otherwise that lock stays held for the rest of the process.
	TEST_F( CoMutexTests, MoveAssignReleasesTheOverwrittenLock ){
		CoMutex a, b;
		auto guardA = a.TryLock();
		auto guardB = b.TryLock();
		ASSERT_TRUE( guardA && guardB );

		guardA = move( guardB );
		EXPECT_TRUE( a.TryLock() ) << "the overwritten lock was left held";
		EXPECT_FALSE( guardB ) << "the moved-from guard still claims the lock";
		EXPECT_FALSE( b.TryLock() ) << "the assigned-to guard did not take the lock over";
		guardB.unlock();//disengaged, so this must not release b.
		EXPECT_FALSE( b.TryLock() ) << "the moved-from guard released a lock it no longer owns";
		guardA.unlock();
		EXPECT_TRUE( b.TryLock() ) << "the surviving guard did not release";

		//self-assignment has to be a no-op:  without the this!=&x check, unlock() runs first and the guard then adopts its
		//own just-cleared pointer, so the lock is released while a live guard still believes it holds it.
		CoMutex c;
		auto self = c.TryLock();
		assignFrom( self, self );
		EXPECT_TRUE( self );
		EXPECT_FALSE( c.TryLock() ) << "self-assignment released the lock";
	}
}
#include <jde/fwk/co/Await.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/process/execution.h>
#include <atomic>
#include <thread>

#define let const auto

namespace Jde::Tests{
	struct BlockAwaitTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()ι->void{ Execution::Run(); }
	};

	//poll until the flag flips or the 10s deadline passes; the ASSERT stays at the call site.
	Ω waitDone( const std::atomic<bool>& done )ι->bool{
		let deadline = steady_clock::now()+10s;
		while( !done && steady_clock::now()<deadline )
			std::this_thread::sleep_for( 5ms );
		return done;
	}

	//named apart from AnyAwaitTests' TestException on purpose: both files share this namespace, and two
	//different classes under one name across TUs is an ODR violation nothing would report.
	struct BlockException final : Exception{
		BlockException( string what, SRCE )ι:Exception{ move(what), {}, sl }{}
		α Move()ι->up<Exception> override{ return mu<BlockException>( move(*this) ); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};

	//both resume from their own thread, so the blocked caller genuinely waits and the notify races its wake.
	struct ThreadedInt final : TAwait<int>{
		ThreadedInt( int v, SRCE )ι:TAwait<int>{sl},_v{v}{}
		α Suspend()ι->void override{ std::thread{ [this]{ Resume( int{_v} ); } }.detach(); }
		int _v;
	};
	struct ThreadedThrower final : VoidAwait{
		ThreadedThrower( SRCE )ι:VoidAwait{sl}{}
		α Suspend()ι->void override{ std::thread{ [this]{ ResumeExp( BlockException{"blocked boom"} ); } }.detach(); }
	};

	TEST_F( BlockAwaitTests, TAwaitReturnsTheValue ){
		EXPECT_EQ( BlockTAwait(ThreadedInt{42}), 42 );
	}

	//the bridge unwinds through BlockAwaitState::Error, so the concrete type has to survive Move() there and
	//Throw() back out - a slice would hand every one of the 143 call sites a bare Exception instead.
	TEST_F( BlockAwaitTests, VoidAwaitRethrowsTheSubclass ){
		bool caught{};
		try{
			BlockVoidAwait( ThreadedThrower{} );
		}
		catch( BlockException& ){ caught = true; }
		catch( Exception& ){ caught = false; }
		EXPECT_TRUE( caught ) << "the exception came back sliced to a base type";
	}

	//blocking from an executor thread is the normal case downstream (LocalQL::Upsert, ScalerSync): it must
	//complete rather than wedge the thread it was posted to.
	TEST_F( BlockAwaitTests, BlocksFromAnExecutorThread ){
		auto done = ms<std::atomic<bool>>();
		auto value = ms<std::atomic<int>>();
		Post( [done, value]{ *value = BlockTAwait( ThreadedInt{7} ); *done = true; } );
		ASSERT_TRUE( waitDone(*done) ) << "the executor thread never came back from BlockTAwait";
		EXPECT_EQ( value->load(), 7 );
	}

	//BlockAwaitState is co-owned because the waiter can wake between Signal's assignment and its notify_all - a
	//stack-owned state would be destroyed while notify_all still touched it.  Repeat enough to hit that window.
	TEST_F( BlockAwaitTests, RepeatedBlocksDoNotRaceTheNotify ){
		for( int i=0; i<300; ++i )
			ASSERT_EQ( BlockTAwait(ThreadedInt{i}), i ) << "iteration " << i;
	}

	struct SlowInt final : TAwait<int>{
		SlowInt( Duration delay, SRCE )ι:TAwait<int>{sl},_delay{delay}{}
		α Suspend()ι->void override{ std::thread{ [this]{ std::this_thread::sleep_for(_delay); Resume(int{9}); } }.detach(); }
		Duration _delay;
	};

	//A response that is dropped rather than delivered parks the caller with nothing in the log to say so - 7m33s of it in the
	//2026-08-07 gateway stall.  The wait still never gives up; past /workers/blockStallWarning (200ms in this suite's config,
	//30s in production) it just says what it is waiting on.  reviews/gateway-review.md #36.
	TEST_F( BlockAwaitTests, WarnsWhileStalled ){
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		EXPECT_EQ( BlockTAwait(SlowInt{700ms}), 9 ) << "the warning must not disturb the result";
		let warnings = logger.Find( [](let& entry){ return entry.Message().starts_with("BlockAwait has been waiting"); } );
		EXPECT_GE( warnings.size(), 1u ) << "a stalled block logged nothing";
	}

	//install-issues (the 09-15 rerun's retry table): a wait the caller knows to be long - the OpcServer's startup blocked on a
	//hub not up yet - read like a hang: a warning at 30 s, criticals after.  The caller caps the level; the lines still come,
	//one per interval, so a reader sees the wait, at the level the caller chose.
	TEST_F( BlockAwaitTests, StallLevelCapped ){
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		EXPECT_EQ( BlockTAwait(SlowInt{700ms}, ELogLevel::Information), 9 );
		let lines = logger.Find( [](let& entry){ return entry.Message().starts_with("BlockAwait has been waiting"); } );
		ASSERT_GE( lines.size(), 2u ) << "700ms over a 200ms interval should say so more than once";
		for( let& line : lines )
			EXPECT_EQ( line.Level, ELogLevel::Information ) << line.Message();
	}
}

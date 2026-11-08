#include <atomic>
#include <semaphore>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>
#include <jde/tests/Capture.h>
#include <jde/tests/WaitUntil.h>

#define let const auto

namespace Jde::Tests{
	struct ExecutionTests : public ::testing::Test{};

	//named apart from the other suites' test exceptions: they share this namespace, and two different classes
	//under one name across TUs is an ODR violation nothing would report.
	struct PostException final : Exception{
		PostException( string what, SRCE )ι:Exception{ move(what), {ELogLevel::Debug}, sl }{}
		α Move()ι->up<Exception> override{ return mu<PostException>( move(*this) ); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};

	//Each handler holds its thread until all have arrived, so the names are those of distinct workers.  Null if they never did.
	Ω heldWorkerNames()ι->optional<flat_set<string>>{
		struct Held final{
			mutex Mutex;
			flat_set<string> Names;
			atomic<uint> Arrived{};
			atomic_flag Release;
		};
		auto held = ms<Held>();
		let count = ThreadCount();
		for( uint i=0; i<count; ++i )
			Post( [held]{
				{
					lg _{ held->Mutex };
					held->Names.insert( Thread::Name() );
				}
				++held->Arrived;
				held->Release.wait( false );
			} );
		let allArrived = WaitUntil( [&]{ return held->Arrived.load()==count; }, 10s, 1ms );
		held->Release.test_and_set();
		held->Release.notify_all();
		if( !allArrived )
			return nullopt;
		lg _{ held->Mutex };
		return held->Names;
	}
	Ω expectedWorkerNames()ι->flat_set<string>{
		flat_set<string> y;
		for( uint i=0; i<ThreadCount(); ++i )
			y.insert( Ƒ("Ex[{}]", i) );
		return y;
	}
	TEST_F( ExecutionTests, EveryWorkerRunsUnderItsName ){
		EXPECT_EQ( heldWorkerNames(), expectedWorkerNames() );
	}

	TEST_F( ExecutionTests, OnThreadIsTrueOnlyOnAWorker ){
		EXPECT_FALSE( Execution::OnThread() );
		EXPECT_TRUE( Execution::Running() );
		auto onThread = ms<std::atomic<int>>( -1 );
		Post( [onThread]{ *onThread = Execution::OnThread(); } );
		ASSERT_TRUE( WaitUntil([&]{ return onThread->load()!=-1; }, 10s, 1ms) );
		EXPECT_EQ( onThread->load(), 1 );
	}

	TEST_F( ExecutionTests, AThrowingHandlerLeavesEveryWorkerRunning ){
		Logging::Capture capture{ ELogLevel::Error };
		let count = ThreadCount();
		for( uint i=0; i<count; ++i ){
			Post( []{ throw PostException{ "posted throw" }; } );
			Post( []{ throw 1; } );
		}
		ASSERT_TRUE( WaitUntil([&]{
			return capture.Find("posted throw").size()==count
				&& capture.Find("An Executor event threw.").size()==count*2;
		}, 10s, 1ms) ) << "the handlers' exceptions were not all caught";
		EXPECT_EQ( heldWorkerNames(), expectedWorkerNames() );
	}

	//Post( Handle&&, Exception&& ) is the executor's error-resume path - one caller repo-wide (Subscriptions) and
	//no test anywhere.  Every sibling path (ResumeExp, FileOp::Complete, BlockAwait) keeps the concrete type.
	struct PostThrower final : VoidAwait{
		PostThrower( SRCE )ι:VoidAwait{sl}{}
		//a *copy* of the handle, which is the production shape (Subscriptions posts handles held in its own
		//vector).  Post nulls what it is handed, and the awaitable still needs its own _h: await_resume reaches
		//the stored exception through Promise(), so Post(move(_h),…) resumes the awaiter as if it had succeeded.
		α Suspend()ι->void override{ Post( Handle{_h}, PostException{"posted boom"} ); }
	};
	Ω postExceptionTask( sp<std::binary_semaphore> done, sp<std::atomic<bool>> subclass, sp<string> what )ι->VoidTask{
		try{
			co_await PostThrower{};
		}
		catch( PostException& e ){ *subclass = true; *what = e.what(); }
		catch( Exception& e ){ *subclass = false; *what = e.what(); }
		done->release();
	}
	TEST_F( ExecutionTests, PostResumesWithTheException ){
		auto done = ms<std::binary_semaphore>( 0 );
		auto subclass = ms<std::atomic<bool>>();
		auto what = ms<string>();
		postExceptionTask( done, subclass, what );
		ASSERT_TRUE( done->try_acquire_for(10s) ) << "the posted exception never resumed the coroutine";
		EXPECT_EQ( *what, "posted boom" );
		EXPECT_TRUE( subclass->load() ) << "the exception arrived sliced to a base type";
	}
}
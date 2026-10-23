#include <semaphore>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/process/execution.h>
#include <jde/tests/WaitUntil.h>
#include <atomic>
#include <thread>

#define let const auto

namespace Jde::Tests{
	struct ExecutionTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()ι->void{ Execution::Run(); }
	};

	//named apart from the other suites' test exceptions: they share this namespace, and two different classes
	//under one name across TUs is an ODR violation nothing would report.
	struct PostException final : Exception{
		PostException( string what, SRCE )ι:Exception{ move(what), {ELogLevel::Debug}, sl }{}
		α Move()ι->up<Exception> override{ return mu<PostException>( move(*this) ); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};

	// PostIO exists for the io strand: the io_uring completion path treats _armed as strand-confined, so a plain
	// context post would let two handlers run at once.  Asio guarantees the serialization - what this pins is the
	// wiring, that PostIO goes through the strand and not the context.  The counter is deliberately not atomic.
	TEST_F( ExecutionTests, PostIOSerializes ){
		constexpr uint threadCount{ 8 }, perThread{ 125 };
		auto counter = ms<uint>( 0 );
		auto inside = ms<std::atomic<bool>>();
		auto overlapped = ms<std::atomic<bool>>();
		auto completed = ms<std::atomic<uint>>();
		{
			vector<std::jthread> threads;
			for( uint t=0; t<threadCount; ++t )
				threads.emplace_back( [=]{
					for( uint i=0; i<perThread; ++i )
						PostIO( [=]{
							if( inside->exchange(true) )
								*overlapped = true;
							++*counter;
							*inside = false;
							++*completed;
						} );
				} );
		}//jthreads join here - every handler is queued.
		ASSERT_TRUE( WaitUntil([&]{ return completed->load()==threadCount*perThread; }, 10s, 1ms) ) << "only " << completed->load() << " of " << threadCount*perThread << " handlers ran";
		EXPECT_FALSE( overlapped->load() ) << "two PostIO handlers ran at once - not serialized by the strand";
		EXPECT_EQ( *counter, threadCount*perThread ) << "a non-atomic increment lost a count, so the handlers overlapped";
	}

	//Post( Handle&&, Exception&& ) is the executor's error-resume path - one caller repo-wide (Subscriptions) and
	//no test anywhere.  Every sibling path (ResumeExp, FileIOArg::PostExp, BlockAwait) keeps the concrete type.
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
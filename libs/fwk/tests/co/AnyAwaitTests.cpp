#include <jde/fwk/co/AllAwait.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/log/MemoryLog.h>
#include <atomic>
#include <latch>
#include <thread>

#define let const auto

namespace Jde::Tests{
	struct AnyAwaitTests : public ::testing::Test{};

	Ω settle( const std::atomic<uint>& completed, uint expected )->bool{
		let deadline = steady_clock::now()+5s;
		while( completed!=expected && steady_clock::now()<deadline )
			std::this_thread::sleep_for( 1ms );
		return completed==expected;
	}

	struct TestException final : Exception{
		TestException( string what, SRCE )ι:Exception{ move(what), {}, sl }{}
		α Move()ι->up<Exception> override{ return mu<TestException>( move(*this) ); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};

	//new-style awaitables - any coroutine can co_await them.
	template<class T>
	struct InlineAny final : AnyAwait<T>{
		InlineAny( T v, SRCE )ι:AnyAwait<T>{sl},_v{move(v)}{}
		α Suspend()ι->void override{ this->Resume( move(_v) ); }	//resumes the awaiter inline from await_suspend - the nested-resume path.
		T _v;
	};
	struct InlineVoidAny final : AnyVoidAwait{
		InlineVoidAny( SRCE )ι:AnyVoidAwait{sl}{}
		α Suspend()ι->void override{ Resume(); }
	};
	template<class T>
	struct ThreadedAny final : AnyAwait<T>{
		ThreadedAny( T v, steady_clock::duration delay=1ms, std::atomic<bool>* done=nullptr, SRCE )ι:AnyAwait<T>{sl},_v{move(v)},_delay{delay},_done{done}{}
		α Suspend()ι->void override{
			std::thread{ [this]{
				std::this_thread::sleep_for( _delay );	//force a real suspension before the resume.
				if( _done )
					*_done = true;
				this->Resume( move(_v) );
			} }.detach();
		}
		T _v; steady_clock::duration _delay; std::atomic<bool>* _done;
	};
	struct ReadyAny final : AnyAwait<int>{
		ReadyAny( int v, SRCE )ι:AnyAwait<int>{sl}{ _result = v; }
		α await_ready()ι->bool override{ return true; }
		α Suspend()ι->void override{ _suspended = true; }
		bool _suspended{};
	};
	struct ReadyErrorAny final : AnyVoidAwait{
		ReadyErrorAny( SRCE )ι:AnyVoidAwait{sl}{ _error = TestException{"pre-completed"}.Move(); }
		α await_ready()ι->bool override{ return true; }
		α Suspend()ι->void override{ _suspended = true; }
		bool _suspended{};
	};

	//old-style awaitables - bridged with Any().
	struct OldInt final : TAwait<int>{
		OldInt( int v, SRCE )ι:TAwait<int>{sl},_v{v}{}
		α Suspend()ι->void override{ std::thread{ [this]{ Resume( int{_v} ); } }.detach(); }
		int _v;
	};
	struct OldVoid final : VoidAwait{
		OldVoid( SRCE )ι:VoidAwait{sl}{}
		α Suspend()ι->void override{ Resume(); }
	};
	struct OldThrower final : VoidAwait{
		OldThrower( SRCE )ι:VoidAwait{sl}{}
		α Suspend()ι->void override{ ResumeExp( TestException{"old-style boom"} ); }
	};

	//One coroutine awaits int, string, and void awaitables - with the IAwait family each value type would
	//need its own coroutine (the awaitable's ::Task must be the caller's return type).
	Ω mixedVoidTask( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		let i = co_await InlineAny<int>{ 42 };
		let s = co_await InlineAny<string>{ "abc" };
		co_await InlineVoidAny{};
		ok = i==42 && s=="abc";
		++completed;
	}
	Ω mixedTypedTask( std::atomic<uint>& completed, std::atomic<bool>& ok )->TTask<uint32>{
		let i = co_await InlineAny<int>{ 7 };	//awaited value type != the task's result type - the caller's promise is not the mailbox here.
		ok = i==7;
		++completed;
	}
	TEST_F( AnyAwaitTests, MixedTypesInOneCoroutine ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{}, okTyped{};
		mixedVoidTask( completed, ok );
		mixedTypedTask( completed, okTyped );
		ASSERT_TRUE( settle(completed, 2) ) << "a coroutine never completed";
		EXPECT_TRUE( ok.load() );
		EXPECT_TRUE( okTyped.load() );
	}

	Ω adapterBridges( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		let i = co_await Any( OldInt{7} );	//rvalue - the adapter owns the inner awaitable.
		OldInt local{ 9 };
		let j = co_await Any( local );	//lvalue - the adapter holds a reference.
		co_await Any( OldVoid{} );
		ok = i==7 && j==9;
		++completed;
	}
	TEST_F( AnyAwaitTests, AdapterBridgesExistingAwaitables ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		adapterBridges( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "the adapter never resumed its awaiter";
		EXPECT_TRUE( ok.load() );
	}

	//The relay is promise.SetExp -> glue catch -> adapter ResumeExp -> await_resume rethrow; every hop uses
	//the virtual Move/Throw, so the concrete type must survive to the handler.
	Ω exceptionSurvives( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		try{
			co_await Any( OldThrower{} );
		}
		catch( TestException& ){ ok = true; }
		catch( Exception& ){ ok = false; }	//sliced somewhere in the relay.
		++completed;
	}
	TEST_F( AnyAwaitTests, ExceptionSubclassSurvivesTheAdapter ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		exceptionSurvives( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "the exception path never resumed the awaiter";
		EXPECT_TRUE( ok.load() ) << "TestException arrived sliced to a base type";
	}

	Ω crossThread( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		let s = co_await ThreadedAny<string>{ "from another thread" };
		let i = co_await Any( OldInt{3} );	//the old-style inner also resumes from its own thread.
		ok = s=="from another thread" && i==3;
		++completed;
	}
	TEST_F( AnyAwaitTests, CrossThreadResume ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		crossThread( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "a cross-thread resume was lost";
		EXPECT_TRUE( ok.load() );
	}

	//await_ready()==true consumes the pre-set result/error without ever suspending - possible because the
	//storage lives in the awaitable; the IAwait family has nowhere to park a ready value.
	Ω preCompleted( std::atomic<uint>& completed, std::atomic<bool>& ok, std::atomic<bool>& suspendCalled )->VoidTask{
		ReadyAny ready{ 5 };
		let i = co_await ready;
		bool threw{};
		ReadyErrorAny error;
		try{
			co_await error;
		}
		catch( TestException& ){ threw = true; }
		ok = i==5 && threw;
		suspendCalled = ready._suspended || error._suspended;
		++completed;
	}
	TEST_F( AnyAwaitTests, PreCompletedShortCircuits ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{}, suspendCalled{};
		preCompleted( completed, ok, suspendCalled );
		ASSERT_TRUE( settle(completed, 1) );
		EXPECT_TRUE( ok.load() );
		EXPECT_FALSE( suspendCalled.load() ) << "await_ready()==true must skip Suspend entirely";
	}

	//the runtime_error overloads, which wrap a non-Jde exception instead of dropping it.  Every ResumeExp in the
	//suite took the Exception&& overload, so the wrapping had no coverage on either the promise or the adapter.
	struct OldPlainThrower final : VoidAwait{
		OldPlainThrower( SRCE )ι:VoidAwait{sl}{}
		α Suspend()ι->void override{ ResumeExp( std::runtime_error{"plain runtime_error"} ); }
	};
	Ω plainThrowDirect( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		try{
			co_await OldPlainThrower{};
		}
		catch( const Exception& e ){ ok = string{e.what()}.contains("plain runtime_error"); }
		catch( ... ){ ok = false; }
		++completed;
	}
	Ω plainThrowAdapted( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		try{
			co_await Any( OldPlainThrower{} );
		}
		catch( const Exception& e ){ ok = string{e.what()}.contains("plain runtime_error"); }
		catch( ... ){ ok = false; }
		++completed;
	}
	TEST_F( AnyAwaitTests, RuntimeErrorIsWrapped ){
		std::atomic<uint> completed{}; std::atomic<bool> direct{}, adapted{};
		plainThrowDirect( completed, direct );
		plainThrowAdapted( completed, adapted );
		ASSERT_TRUE( settle(completed, 2) ) << "a coroutine never completed";
		EXPECT_TRUE( direct.load() ) << "SetExp(runtime_error&&) must wrap a non-Jde exception, not drop it";
		EXPECT_TRUE( adapted.load() ) << "and it must survive the adapter relay";
	}

	//the adapter's catch(...): nothing may escape the glue coroutine or the awaiting one is stranded forever.
	//await_resume is the only place a non-runtime_error can come from - Suspend is noexcept, so a throw there
	//terminates rather than reaching any handler.
	struct OldLogicThrower final : TAwait<int>{
		OldLogicThrower( SRCE )ι:TAwait<int>{sl}{}
		α Suspend()ι->void override{ Resume( 1 ); }
		α await_resume()ε->int override{ throw std::logic_error{"a logic_error is not a runtime_error"}; }
	};
	Ω logicThrow( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		try{
			co_await Any( OldLogicThrower{} );
		}
		catch( const Exception& e ){ ok = string{e.what()}.contains("Unknown exception"); }
		catch( ... ){ ok = false; }
		++completed;
	}
	TEST_F( AnyAwaitTests, UnknownExceptionResumesTheAwaiter ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		logicThrow( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "the awaiting coroutine was stranded by the unknown exception";
		EXPECT_TRUE( ok.load() );
	}

	//An exception escaping a coroutine body lands in IPromise::unhandled_exception, on a promise nobody will read
	//(final_suspend is suspend_never), so the log line is the only trace it leaves.  A Jde::Exception logs itself
	//at construction and would be counted whether or not unhandled_exception ran - hence a plain runtime_error,
	//which that handler wraps into a fresh Critical one.
	Ω escapingTask( sp<std::atomic<bool>> reached )->VoidTask{
		*reached = true;
		if( reached->load() )
			throw std::runtime_error{ "escaped-from-coroutine" };
		co_return;
	}
	TEST_F( AnyAwaitTests, UnhandledExceptionLogsCriticalAndSurvives ){
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		auto reached = ms<std::atomic<bool>>();
		escapingTask( reached );
		EXPECT_TRUE( reached->load() );
		let critical = logger.Find( function<bool(const Logging::Entry&)>{[]( const Logging::Entry& e ){
			return e.Level==ELogLevel::Critical && e.Message().contains( "escaped-from-coroutine" );
		}} );
		EXPECT_EQ( critical.size(), 1u ) << "the escaping exception must be logged Critical, not swallowed";
	}

	//Several at once - All() (AllAwait.h):  slots of both families, unwrapped, from a void and from a typed coroutine;  a failure waits for
	//the rest, and the first by slot order is what is thrown;  every slot is in flight before any finishes;  and the all-inline path, the
	//awaiter running to completion inside Suspend - sqlite's selects complete in await_ready, so that is the gateway suite's production path.
	struct AllAwaitTests : public ::testing::Test{};

	static_assert( std::is_same_v<AllGlueTask<OldInt>::type, TTask<int>> );//IAwait family:  the inner's own task, the pairing rule.
	static_assert( std::is_same_v<AllGlueTask<OldVoid>::type, VoidTask> );
	static_assert( std::is_same_v<AllGlueTask<InlineAny<int>>::type, VoidTask> );//Any family:  any coroutine may await it.
	static_assert( std::is_same_v<decltype(All(std::declval<OldInt>(), std::declval<InlineAny<string>&>(), std::declval<OldVoid>())), AllAwait<OldInt,InlineAny<string>&,OldVoid>> );//rvalues owned, the lvalue referenced.
	static_assert( std::is_same_v<AllAwait<OldInt,InlineAny<string>&,OldVoid>::TResult, tuple<int,string,std::monostate>> );//a void slot keeps its position.

	//All() slots:  one that fails from its own thread after `delay`;  one that cannot finish until every slot sharing its latch has started.
	//The latch is shared so the last thread out destroys it, not the test frame.
	struct DelayedThrowAny final : AnyVoidAwait{
		DelayedThrowAny( string what, steady_clock::duration delay, SRCE )ι:AnyVoidAwait{sl},_what{move(what)},_delay{delay}{}
		α Suspend()ι->void override{ std::thread{ [this]{ std::this_thread::sleep_for( _delay ); ResumeExp( TestException{_what} ); } }.detach(); }
		string _what; steady_clock::duration _delay;
	};
	struct GatedAny final : AnyAwait<int>{
		GatedAny( int v, sp<std::latch> started, SRCE )ι:AnyAwait<int>{sl},_v{v},_started{move(started)}{}
		α Suspend()ι->void override{ std::thread{ [this, started=_started]{ started->arrive_and_wait(); Resume( int{_v} ); } }.detach(); }
		int _v; sp<std::latch> _started;
	};

	Ω allMixedVoid( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		auto [i, s, _, j] = co_await All( ThreadedAny<int>{1}, InlineAny<string>{"abc"}, OldVoid{}, OldInt{7} );//Any and IAwait slots, a void one, none wrapped.
		ok = i==1 && s=="abc" && j==7;
		++completed;
	}
	Ω allMixedTyped( std::atomic<uint>& completed, std::atomic<bool>& ok )->TTask<uint32>{
		auto [i, s] = co_await All( OldInt{3}, ThreadedAny<string>{"x"} );//neither slot's type is the task's result type.
		ok = i==3 && s=="x";
		++completed;
	}
	TEST_F( AllAwaitTests, MixedFamiliesFromVoidAndTypedTasks ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{}, okTyped{};
		allMixedVoid( completed, ok );
		allMixedTyped( completed, okTyped );
		ASSERT_TRUE( settle(completed, 2) ) << "a coroutine never completed";
		EXPECT_TRUE( ok.load() );
		EXPECT_TRUE( okTyped.load() );
	}

	Ω allWaitsForTheSlower( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		std::atomic<bool> slowDone{};
		try{
			co_await All( DelayedThrowAny{"fast boom", 1ms}, ThreadedAny<int>{5, 50ms, &slowDone} );
			ok = false;
		}
		catch( TestException& e ){ ok = slowDone.load() && string{e.what()}.contains("fast boom"); }//resumed only once the slower slot had settled too.
		catch( Exception& ){ ok = false; }//sliced.
		++completed;
	}
	TEST_F( AllAwaitTests, WaitsForTheSlowerSlotWhenOneFails ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		allWaitsForTheSlower( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "the awaiter never resumed";
		EXPECT_TRUE( ok.load() ) << "a failure must wait for the other slots - nothing can be cancelled - and arrive as its own type";
	}

	Ω allThrowsFirstBySlot( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		try{
			co_await All( DelayedThrowAny{"slot0", 50ms}, DelayedThrowAny{"slot1", 1ms}, InlineAny<int>{1} );//slot1 fails first in time.
			ok = false;
		}
		catch( TestException& e ){ ok = string{e.what()}.contains("slot0"); }
		catch( Exception& ){ ok = false; }
		++completed;
	}
	TEST_F( AllAwaitTests, ThrowsTheFirstFailingSlotBySlotOrder ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		allThrowsFirstBySlot( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "the awaiter never resumed";
		EXPECT_TRUE( ok.load() ) << "the thrown failure must be the first by slot, not the first in time";
	}

	Ω allGated( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		auto started = ms<std::latch>( 3 );
		auto [a, b, c] = co_await All( GatedAny{1, started}, GatedAny{2, started}, GatedAny{3, started} );//none can finish until all three have started.
		ok = a==1 && b==2 && c==3;
		++completed;
	}
	TEST_F( AllAwaitTests, StartsEverySlotBeforeAnyFinishes ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		allGated( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "a slot waited on an earlier one finishing - the slots did not all start first";
		EXPECT_TRUE( ok.load() );
	}

	Ω allInline( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		auto [i, j, _, _] = co_await All( InlineAny<int>{4}, ReadyAny{5}, InlineVoidAny{}, OldVoid{} );//every slot completes inside its own launch.
		ok = i==4 && j==5;
		++completed;
	}
	TEST_F( AllAwaitTests, CompletesInlineInsideSuspend ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		allInline( completed, ok );
		EXPECT_EQ( completed.load(), 1u ) << "no thread is involved, so the awaiter must have run to completion inside the launch";//not settle():  that it already happened is the point.
		EXPECT_TRUE( ok.load() );
	}

	TEST_F( AllAwaitTests, BlockAnyBridgesAll ){
		auto [i, j] = BlockAny( All(ThreadedAny<int>{1}, OldInt{2}) );
		EXPECT_EQ( i, 1 );
		EXPECT_EQ( j, 2 );
		EXPECT_THROW( BlockAny(All(OldThrower{}, ThreadedAny<int>{1})), TestException );
	}

	Ω allLvalues( std::atomic<uint>& completed, std::atomic<bool>& ok )->VoidTask{
		OldInt old{ 9 }; InlineAny<int> inl{ 3 };
		auto all = All( old, inl, OldInt{1} );//the lvalues referenced, the rvalue owned.
		static_assert( std::is_same_v<decltype(all), AllAwait<OldInt&,InlineAny<int>&,OldInt>> );
		ok = all.Source().line()==old.Source().line();//the first slot's.
		auto [a, b, c] = co_await all;
		ok = ok && a==9 && b==3 && c==1;
		++completed;
	}
	TEST_F( AllAwaitTests, ReferencesLvalueSlots ){
		std::atomic<uint> completed{}; std::atomic<bool> ok{};
		allLvalues( completed, ok );
		ASSERT_TRUE( settle(completed, 1) ) << "the awaiter never resumed";
		EXPECT_TRUE( ok.load() );
	}

	//the glue's own catch, not AnyAdapterExecute's:  raw IAwait slots whose await_resume throws a foreign exception.  A ResumeExp( runtime_error )
	//can't reach it - IPromise::SetExp has made it an Exception by then.
	struct OldRawThrower final : VoidAwait{
		OldRawThrower( SRCE )ι:VoidAwait{sl}{}
		α Suspend()ι->void override{ Resume(); }
		α await_resume()ε->void override{ throw std::runtime_error{"raw runtime_error"}; }
	};
	Ω allWraps( std::atomic<uint>& completed, std::atomic<bool>& plain, std::atomic<bool>& logic )->VoidTask{
		try{
			co_await All( OldRawThrower{}, InlineAny<int>{1} );
		}
		catch( const Exception& e ){ plain = string{e.what()}.contains("raw runtime_error"); }
		catch( ... ){ plain = false; }
		OldLogicThrower thrower;
		try{
			co_await All( InlineAny<int>{1}, thrower );
		}
		catch( const Exception& e ){ logic = string{e.what()}.contains("a logic_error is not a runtime_error") && e.Source().line()==thrower.Source().line(); }//the failing slot's line, not the first slot's.
		catch( ... ){ logic = false; }
		++completed;
	}
	TEST_F( AllAwaitTests, WrapsAForeignExceptionFromASlot ){
		std::atomic<uint> completed{}; std::atomic<bool> plain{}, logic{};
		allWraps( completed, plain, logic );
		ASSERT_TRUE( settle(completed, 1) ) << "a slot's exception stranded the awaiter";
		EXPECT_TRUE( plain.load() ) << "a runtime_error thrown from a slot's await_resume must keep its text";
		EXPECT_TRUE( logic.load() ) << "and so must a non-runtime_error, with the failing slot's source";
	}
}
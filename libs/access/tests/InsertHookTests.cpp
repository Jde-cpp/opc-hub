//ql-review3 #50: InsertAwait::InsertAfter took `jarray&& result`, a reference into Execute()'s frame - and Execute() returns a
//Task whose initial/final suspend is `suspend_never`, so that frame dies the moment InsertAfter suspends.  Every sibling on the
//chain (UpdateAfter, AddAfter, PurgeAwait::After) already took its payload by value; this was the only one that did not, and it
//was latent purely because no IQLHook in the tree overrides InsertAfter, so MutationAwaits::await_ready always returned true.
//This is that missing hook: one that genuinely suspends, resumed from another thread, so the insert really does hand control
//back to a dead frame before reading `result`.
#include "gtest/gtest.h"
#include <jde/ql/QLHook.h>
#include <thread>
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	//parks in Suspend() and is completed from another thread - the shape no in-tree hook has, and the only one that reaches the bug.
	struct ParkedAwait final : TAwait<jvalue>{
		ParkedAwait( SRCE )ι:TAwait<jvalue>{sl}{}
		α await_ready()ι->bool override{ return false; }
		α Suspend()ι->void override{ std::thread{ [this]{ std::this_thread::sleep_for(20ms); Resume( jvalue{true} ); } }.detach(); }
	};
	//Hook::Add owns it and there is no removal, so it is registered once and stays inert unless a test arms it.
	struct SuspendingInsertHook final : QL::IQLHook{
		α InsertAfter( const QL::MutationQL&, UserPK, uint pk, SL )ι->HookResult override{
			if( !Armed )
				return {};
			++Calls;
			Pk = pk;
			return mu<ParkedAwait>();
		}
		bool Armed{};
		uint Calls{};
		uint Pk{};
	};
	Ω hook()ι->SuspendingInsertHook&{
		static SuspendingInsertHook& y = []()->SuspendingInsertHook&{
			auto h = mu<SuspendingInsertHook>();
			auto& ref = *h;
			QL::Hook::Add( move(h) );
			return ref;
		}();
		return y;
	}

	TEST( InsertHookTests, AnInsertAfterHookThatSuspendsStillGetsTheInsertedRow ){
		let root = GetRoot();
		const string slug{ "review50-user" };
		hook().Armed = true;
		hook().Calls = 0;
		uint32 id{};
		try{
			let y = QL().QuerySync<jobject>( "mutation createUser( name:\""+slug+"\", slug:\""+slug+"\", providerId:1 ){ id }", {}, root );
			id = GetId( y );
			EXPECT_GT( id, 0u ) << serialize( y ); //the payload survived the suspension - it lives in InsertAfter's own frame now.
		}
		catch( ... ){
			hook().Armed = false;
			throw;
		}
		hook().Armed = false;
		EXPECT_EQ( hook().Calls, 1u ) << "the hook never ran, so nothing suspended and this proves nothing";
		EXPECT_EQ( GetId(Select("user", id, root, "id slug", true)), id );
		PurgeUser( UserPK{id}, root );
	}

	//the control: with no hook armed the fan-out is empty, InsertAfter never suspends, and the same insert behaves identically -
	//which is the state every other test in this binary runs in.
	TEST( InsertHookTests, TheSameInsertWithNoHookArmed ){
		let root = GetRoot();
		const string slug{ "review50-plain" };
		hook().Calls = 0;
		let y = QL().QuerySync<jobject>( "mutation createUser( name:\""+slug+"\", slug:\""+slug+"\", providerId:1 ){ id }", {}, root );
		EXPECT_GT( GetId(y), 0u );
		EXPECT_EQ( hook().Calls, 0u );
		PurgeUser( UserPK{GetId(y)}, root );
	}

	//ql-review3 #51: Hook::InsertAfter took its pk as an unnamed parameter and built the pk-less MutationAwaits, so the argument
	//never reached the override.  That is fixed and pinned by QLHookTests.InsertAfterForwardsThePk.
	//End to end the pk is `result[0]["id"]`, which createUser's identity insert, a proc, returns.  Until #223 InsertAwait read it
	//with the json-pointer FindNumber, which "id" is not, so every hook was handed 0.
	TEST( InsertHookTests, TheHookIsToldTheIdInsertAwaitHas ){
		let root = GetRoot();
		const string slug{ "review51-user" };
		hook().Armed = true;
		hook().Calls = 0;
		hook().Pk = 0;
		uint32 id{};
		try{
			id = GetId( QL().QuerySync<jobject>("mutation createUser( name:\""+slug+"\", slug:\""+slug+"\", providerId:1 ){ id }", {}, root) );
		}
		catch( ... ){
			hook().Armed = false;
			throw;
		}
		hook().Armed = false;
		ASSERT_GE( hook().Calls, 1u );
		ASSERT_GT( id, 0u ) << "the reply's id, which comes from the result request rather than the insert";
		EXPECT_EQ( hook().Pk, id );
		PurgeUser( UserPK{id}, root );
	}
}

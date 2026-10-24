//review3 #33:  the QLHook layer had no test in any suite - `grep -rn 'InsertAfter|InsertFailure|PurgeFailure|Hook::Add|IQLHook'
//libs/*/tests apps/*/tests` returned nothing - so nothing checked that an operation reaches its own virtual, that a hook's result
//comes back, or that a failing hook fails the mutation instead of the process.  All of it is testable without a data source: the
//mutations are system-shaped (no DBTable), and every awaitable here resolves in await_ready, so BlockAwait drives the coroutine
//on this thread.
#include <gtest/gtest.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/QLHook.h>
#include <jde/ql/types/Parser.h>
#include <jde/ql/types/TableQL.h>

#define let const auto

namespace Jde::QL::Tests{
	static const vector<sp<DB::AppSchema>> _noHookSchemas;
	Ω mutation( sv command="createStatus", sv args="{id:1}" )ε->MutationQL{
		return MutationQL{ string{command}, Parser::ParseArgs(string{args}), ms<jobject>(), optional<TableQL>{}, true, _noHookSchemas, true };
	}

	//resolves in await_ready, so the hook's result never leaves this thread.
	struct HookValueAwait final : TAwait<jvalue>{
		HookValueAwait( jvalue value, SRCE )ι:TAwait<jvalue>{sl}, _value{move(value)}{}
		α await_ready()ι->bool override{ return true; }
		α Suspend()ι->void override{ ASSERT(false); }
		α await_resume()ε->jvalue override{ return move(_value); }
	private:
		jvalue _value;
	};

	//A hook may not throw:  IQLHook's virtuals are ι, and an override of a non-throwing virtual must be non-throwing too, so a
	//throw inside one terminates before MutationAwaits' catch can see it (that catch is defence against a hook that lies about
	//its specification).  ExceptionAwait<jvalue> (fwk co/Await.h) is the supported channel.
	struct RecordingHook final : IQLHook{
		enum class EMode{ Silent, Value, Throw };
		α Reset()ι->void{ Calls.clear(); Pk = 0; Executer = UserPK{}; Command.clear(); Mode = EMode::Silent; }
		α Record( sv name, const MutationQL& m, UserPK executer )ι->HookResult{
			Calls.emplace_back( name );
			Command = m.CommandName;
			Executer = executer;
			return Result( name );
		}
		α Result( sv name )ι->HookResult{
			using enum EMode;
			switch( Mode ){
			case Value: return mu<HookValueAwait>( jvalue{string{name}} );
			case Throw: return mu<ExceptionAwait<jvalue>>( mu<Exception>(Ƒ("{} refused", name)) );
			case Silent: break;
			}
			return {};
		}
		α Select( const TableQL& t, UserPK executer, SL )ι->HookResult override{ Calls.emplace_back("Select"); Command = t.JsonName; Executer = executer; return Result("Select"); }
		α InsertBefore( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "InsertBefore", m, e ); }
		α InsertAfter( const MutationQL& m, UserPK e, uint pk, SL )ι->HookResult override{ Pk = pk; return Record( "InsertAfter", m, e ); }
		α InsertFailure( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "InsertFailure", m, e ); }
		α UpdateBefore( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "UpdateBefore", m, e ); }
		α UpdateAfter( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "UpdateAfter", m, e ); }
		α PurgeBefore( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "PurgeBefore", m, e ); }
		α PurgeAfter( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "PurgeAfter", m, e ); }
		α PurgeFailure( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "PurgeFailure", m, e ); }
		α AddBefore( const MutationQL& m, UserPK e, std::source_location )ι->HookResult override{ return Record( "AddBefore", m, e ); }
		α Add( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "Add", m, e ); }
		α AddAfter( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "AddAfter", m, e ); }
		α Remove( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "Remove", m, e ); }
		α RemoveAfter( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "RemoveAfter", m, e ); }
		α Start( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "Start", m, e ); }
		α Stop( const MutationQL& m, UserPK e, SL )ι->HookResult override{ return Record( "Stop", m, e ); }

		vector<string> Calls;
		uint Pk{};
		UserPK Executer{};
		string Command;
		EMode Mode{ EMode::Silent };
	};
	//Hook::Add owns the hook and there is no removal, so it is registered once for the process and reset around every test -
	//left inert (Silent, returning nothing), which is what every other suite in this binary already assumes.
	Ω hook()ι->RecordingHook&{
		static RecordingHook& y = []()->RecordingHook& {
			auto h = mu<RecordingHook>();
			auto& ref = *h;
			Hook::Add( move(h) );
			return ref;
		}();
		return y;
	}
	Ω run( MutationAwaits&& a )ε->optional<jarray>{ return BlockAwait( move(a) ); }
	Ω run( QueryHookAwaits&& a )ε->optional<jvalue>{ return BlockAwait( move(a) ); } //a helper, not a local: the ',' in the template arguments would end an EXPECT_ macro's first argument.

	struct QLHookTests : ::testing::Test{
		α SetUp()->void override{ hook().Reset(); }
		α TearDown()->void override{ hook().Reset(); }
	};

	//Each Hook::X names the IQLHook virtual it asks by member pointer (it used to be an Operation bitmask switched back into a
	//virtual), and a wrong one is a hook that simply never fires - #52 is exactly that, one layer up.  Every entry point, once, asserting the virtual it landed on and that the
	//hook's own result came back.
	TEST_F( QLHookTests, EveryOperationReachesItsOwnVirtual ){
		hook().Mode = RecordingHook::EMode::Value;
		let m = mutation();
		let executer = UserPK{ 7 };
		let check = [&]( sv expected, optional<jarray> y ){
			EXPECT_EQ( hook().Calls, vector<string>{string{expected}} );
			ASSERT_TRUE( y ) << expected;
			ASSERT_EQ( y->size(), 1u ) << expected;
			EXPECT_EQ( y->front().as_string(), expected );
			EXPECT_EQ( hook().Executer, executer ) << expected; //the hook is told who asked.
			EXPECT_EQ( hook().Command, "createStatus" ) << expected;
			hook().Reset();
			hook().Mode = RecordingHook::EMode::Value;
		};
		check( "InsertBefore", run(Hook::InsertBefore(m, executer)) );
		check( "InsertAfter", run(Hook::InsertAfter(42, m, executer)) );
		check( "InsertFailure", run(Hook::InsertFailure(m, executer)) ); //neither Failure hook is reached by any other test.
		check( "UpdateBefore", run(Hook::UpdateBefore(m, executer)) );
		check( "UpdateAfter", run(Hook::UpdateAfter(m, executer)) );
		check( "PurgeBefore", run(Hook::PurgeBefore(m, executer)) );
		check( "PurgeAfter", run(Hook::PurgeAfter(m, executer)) );
		check( "PurgeFailure", run(Hook::PurgeFailure(m, executer)) );
		check( "AddBefore", run(Hook::AddBefore(m, executer)) );
		check( "Add", run(Hook::Add(m, executer)) );
		check( "AddAfter", run(Hook::AddAfter(m, executer)) );
		check( "Remove", run(Hook::Remove(m, executer)) );
		check( "RemoveAfter", run(Hook::RemoveAfter(m, executer)) );
		check( "Start", run(Hook::Start(m, executer)) );
		check( "Stop", run(Hook::Stop(m, executer)) );
	}

	//#51: Hook::InsertAfter's first parameter was unnamed, and the 4-argument MutationAwaits it built defaulted _pk to 0 - so an
	//InsertAfter override was always told the row it had just made was id 0, although InsertAwait computes and passes the real
	//one.  The redundant constructor pair is what let that compile;  there is one constructor now and no default for pk.
	TEST_F( QLHookTests, InsertAfterForwardsThePk ){
		hook().Mode = RecordingHook::EMode::Value;
		let m = mutation();
		run( Hook::InsertAfter(42, m, UserPK{1}) );
		EXPECT_EQ( hook().Calls.size(), 1u );
		EXPECT_EQ( hook().Pk, 42u );

		hook().Reset();
		hook().Mode = RecordingHook::EMode::Value;
		run( Hook::InsertAfter(0, m, UserPK{1}) );
		EXPECT_EQ( hook().Pk, 0u ); //an insert whose id could not be recovered still says so - 0 is a value, not the default.
	}

	//A hook's only way to refuse:  ExceptionAwait, awaited in HookAwaits::Execute, caught there and re-raised from
	//await_resume.  What must not happen is the mutation quietly succeeding.
	TEST_F( QLHookTests, AHookThatRefusesFailsTheMutation ){
		hook().Mode = RecordingHook::EMode::Throw;
		let m = mutation( "purgeStatus", "{id:1}" );
		try{
			run( Hook::PurgeBefore(m, UserPK{1}) );
			ADD_FAILURE() << "the refusal was swallowed";
		}
		catch( const Exception& e ){
			EXPECT_EQ( string{e.what()}, "PurgeBefore refused" );
		}
		EXPECT_EQ( hook().Calls.size(), 1u );
	}

	//No hook claims the operation -> nullopt, which is how MutationAwait::Start/Stop tell "no handler" from "handled, returned
	//null".  With the hook inert this is the state every other suite in this binary runs in.
	TEST_F( QLHookTests, NoHookResultIsNullopt ){
		let m = mutation( "startStatus", "{}" );
		EXPECT_FALSE( run(Hook::Start(m, UserPK{1})) );
		EXPECT_FALSE( run(Hook::Stop(m, UserPK{1})) );
		EXPECT_EQ( hook().Calls, (vector<string>{"Start","Stop"}) ); //asked, and declined - not skipped.
	}

	//The query side of the same machinery.  QueryHookAwaits unwraps a single result rather than wrapping it in an array.
	TEST_F( QLHookTests, SelectHookIsAskedAndItsResultUnwrapped ){
		TableQL table{ "status", jobject{}, ms<jobject>(), _noHookSchemas, true }; //QueryHookAwaits holds it by reference.
		EXPECT_FALSE( run(Hook::Select(table, UserPK{3})) );
		EXPECT_EQ( hook().Calls, vector<string>{"Select"} );

		hook().Reset();
		hook().Mode = RecordingHook::EMode::Value;
		let y = run( Hook::Select(table, UserPK{3}) );
		ASSERT_TRUE( y );
		EXPECT_EQ( y->as_string(), "Select" ); //the value itself, not [value].
	}
	TEST_F( QLHookTests, SelectHookThatRefusesFailsTheQuery ){
		TableQL table{ "status", jobject{}, ms<jobject>(), _noHookSchemas, true };
		hook().Mode = RecordingHook::EMode::Throw;
		try{
			run( Hook::Select(table, UserPK{3}) );
			ADD_FAILURE() << "the refusal was swallowed";
		}
		catch( const Exception& e ){
			EXPECT_EQ( string{e.what()}, "Select refused" );
		}
	}
}
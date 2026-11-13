//Context (docs/flows/spec.md *Context*):  the node and flow stores as loaded and as the host is told of their changes, and
//the global store, every flow's, from two threads.
#include <numeric>
#include <thread>
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	struct ContextTests : FlowTest{};

	TEST_F( ContextTests, TheStoresLoadAndTellTheHostEachChange ){
		Context = { {"seen", 10} };
		NodeContext = { {"out", jobject{{"seen", 5}}} };
		Run( Nodes({Node("in", "inject", "{}", {{"out", "other"}}), Node("out", "log", R"({"tally":true})"), Node("other", "log", R"({"tally":true})")}) );
		Inject( 1 );
		Inject( 1 );
		lg _{ ChangesMutex };
		EXPECT_EQ( Changes, (vector<string>{"out/seen=6", "/seen=11", "other/seen=1", "/seen=12", "out/seen=7", "/seen=13", "other/seen=2", "/seen=14"}) )
			<< "each node's own count from what was loaded, and the flow's they share";
	}

	TEST_F( ContextTests, AValueSetFromOutsideIsReadAtTheNextGet ){
		NodeContext = { {"out", jobject{{"seen", 5}}} };
		Run( Nodes({Node("in", "inject", "{}", {{"out"}}), Node("out", "log", R"({"tally":true})")}) );
		let set = [&]( sv node, sv key, jvalue value ){
			std::promise<bool> done;
			Deployed->SetContext( node, key, move(value), [&]( bool set ){ done.set_value( set ); } );
			return done.get_future().get();
		};
		EXPECT_TRUE( set("out", "seen", 40) );
		EXPECT_TRUE( set({}, "seen", 100) );
		EXPECT_TRUE( set({}, "gone", nullptr) ) << "a key that is not there has nothing to take";
		EXPECT_FALSE( set("nowhere", "seen", 1) ) << "a node that is not the flow's";
		Inject( 1 );
		{
			lg _{ ChangesMutex };
			EXPECT_EQ( Changes, (vector<string>{"out/seen=40", "/seen=100", "out/seen=41", "/seen=101"}) ) << "the host is told of the set, and the node counts on from it";
		}
		EXPECT_TRUE( set({}, "seen", nullptr) );
		Stop();
		EXPECT_FALSE( set({}, "seen", 7) ) << "a flow that has stopped takes none";
		lg _{ ChangesMutex };
		EXPECT_EQ( Changes.back(), "/seen=-" );
	}

	TEST( Context, Store ){
		vector<string> changes;
		Store store{ jobject{{"a", 1}}, [&]( sv key, const jvalue* value ){ changes.push_back( Ƒ("{}={}", key, value ? serialize(*value) : "-") ); } };
		EXPECT_EQ( *store.Get("a"), 1 );
		EXPECT_FALSE( store.Get("b") );
		store.Set( "b", jarray{1, 2} );
		store.Set( "a", nullptr );
		store.Set( "gone", nullptr );
		EXPECT_FALSE( store.Get("a") );
		EXPECT_EQ( store.Keys(), vector<string>{"b"} );
		EXPECT_EQ( changes, (vector<string>{"b=[1,2]", "a=-"}) ) << "a key that was not there did not go";
	}

	TEST( Context, GlobalGetsACopyAndSetsByExchange ){
		vector<string> changes;
		GlobalStore store{ jobject{{"count", 1}}, [&]( sv key, const jvalue* value ){ changes.push_back( Ƒ("{}={}", key, value ? serialize(*value) : "-") ); } };
		auto copy = store.Get( "count" );
		copy = 99;
		EXPECT_EQ( store.Get("count"), 1 );
		EXPECT_EQ( store.Set("count", 2), 1 ) << "the value before";
		EXPECT_TRUE( store.Set("fresh", "x").is_null() );
		EXPECT_EQ( store.Set("count", nullptr), 2 );
		EXPECT_TRUE( store.Get("count").is_null() );
		EXPECT_EQ( store.Keys(), vector<string>{"fresh"} );
		EXPECT_EQ( changes, (vector<string>{"count=2", "fresh=\"x\"", "count=-"}) );
	}

	TEST( Context, GlobalFromTwoThreads ){
		constexpr int64_t each{ 20000 };
		GlobalStore store{ jobject{{"n", 0}} };
		std::array<vector<int64_t>,2> before;
		{
			std::array<std::jthread,2> threads;
			for( uint t=0; t<2; ++t ){
				threads[t] = std::jthread{ [&, t]{
					for( int64_t i=1; i<=each; ++i ){
						before[t].push_back( store.Set("n", i+(int64_t)t*each).to_number<int64_t>() );
						store.Get( "n" );
					}
				} };
			}
		}
		//Every value set was exchanged out once, but the last, which is still there:  no set was lost and none seen twice.
		vector<int64_t> out{ before[0] };
		out.insert( out.end(), before[1].begin(), before[1].end() );
		out.push_back( store.Get("n").to_number<int64_t>() );
		std::ranges::sort( out );
		vector<int64_t> all( 2*each+1 );
		std::iota( all.begin(), all.end(), 0 );
		EXPECT_TRUE( out==all );
	}

	TEST_F( ContextTests, TwoFlowsShareTheGlobalStore ){
		Run( Nodes({Node("in", "inject", "{}", {{"keep"}}), Node("keep", "change", R"({"rules":[{"t":"set","p":"last","pt":"global","to":"payload","tot":"msg"}]})")}) );
		auto graph = Graph::Read( Nodes({
			Node( "in", "inject", "{}", {{"read"}} ),
			Node( "read", "change", R"({"rules":[{"t":"set","p":"payload","to":"last","tot":"global"}]})", {{"second"}} ),
			Node( "second", "log" )
		}) );
		ASSERT_TRUE( graph.has_value() );
		Flow reader{ move(*graph), {.Name="reader", .Clock=Time, .Host=Host, .Global=Global} };
		ASSERT_TRUE( reader.Start() );
		Inject( jobject{{"rpm", 1450}} );
		reader.Inject( "in" );
		std::promise<void> done;
		reader.Post( [&]{ done.set_value(); } );
		done.get_future().get();
		EXPECT_EQ( Took("second"), R"({"rpm":1450})" );
		std::promise<void> stopped;
		reader.Stop( [&]{ stopped.set_value(); } );
		stopped.get_future().get();
	}
}
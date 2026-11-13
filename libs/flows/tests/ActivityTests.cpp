//The live activity (docs/flows/spec.md *Live*):  what a watched flow keeps between two takes - each node's figures, its
//status change and its last error, and each wire's count and first message - its cue, and a flow that is not watched.
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	struct ActivityTests : FlowTest{
		//A wire's activity as `from:port>to count payload`, by the graph's ids.
		α Wires( const Activity& activity, const jvalue& graph )->vector<string>{
			let& nodes = graph.at( "nodes" ).as_array();
			let id = [&]( uint32 index ){ return string{ nodes.at(index).at("id").as_string() }; };
			vector<string> y;
			for( let& wire : activity.Wires )
				y.push_back( Ƒ("{}:{}>{} {} {}", id(wire.Node), wire.Port, id(wire.To), wire.Count, serialize(wire.First.Payload())) );
			return y;
		}
		Ω Of( const Activity& activity, uint32 node )->const NodeActivity*{
			let p = find( activity.Nodes, node, &NodeActivity::Node );
			return p==activity.Nodes.end() ? nullptr : &*p;
		}
	};

	TEST_F( ActivityTests, AWatchedFlowKeepsEachWiresCountAndFirstMessage ){
		let graph = Nodes({
			Node( "in", "inject", "{}", {{"a", "b"}} ),
			Node( "a", "change", R"({"rules":[{"t":"set","p":"payload","to":"changed"}]})", {{"out"}} ),
			Node( "b", "log" ), Node( "out", "log" )
		});
		Run( graph );
		Inject( 0 );
		EXPECT_FALSE( Deployed->TakeActivity().Any() ) << "nothing is kept for a flow nobody watches";
		EXPECT_EQ( Cues, 0u );

		Deployed->Watch( true );
		Advance( 5s );
		Inject( 1 );
		Advance( 5s );
		Inject( 2 );
		EXPECT_EQ( Cues, 1u ) << "told once, at the first thing to take";
		FlowState before;
		let activity = Deployed->TakeActivity( &before );
		EXPECT_EQ( Wires(activity, graph), (vector<string>{"in:0>a 2 1", "in:0>b 2 1", "a:0>out 2 \"changed\""}) ) << "the first since the take, one copy a take";
		ASSERT_EQ( activity.Wires.size(), 3u );
		let sent = Host->Took->Of( "out" ).at( 1 );
		EXPECT_EQ( activity.Wires[0].First.Id(), sent.at("_id").to_number<MessageId>() ) << "the message itself down the first wire";
		EXPECT_EQ( activity.Wires[1].First.Parent(), activity.Wires[0].First.Id() ) << "and its clone down the second";
		EXPECT_EQ( activity.Wires[0].Time, At(6, 0, 5) ) << "when the first went, on the flow's clock";
		ASSERT_EQ( activity.Nodes.size(), 4u );
		EXPECT_EQ( activity.Nodes[0].Counts.Sent, vector<uint>{2} );
		EXPECT_EQ( activity.Nodes[1].Counts.Received, 2u );
		EXPECT_EQ( activity.Nodes[1].Counts.Sent, vector<uint>{2} );
		EXPECT_EQ( activity.Nodes[3].Counts.Received, 2u );
		EXPECT_FALSE( activity.Nodes[3].Status );
		ASSERT_EQ( before.Counts.size(), 4u );
		EXPECT_EQ( before.Counts[3].Received, 3u ) << "the flow as it stands is since its start, the unwatched message too";
		EXPECT_EQ( before.Statuses.size(), 4u );

		EXPECT_FALSE( Deployed->TakeActivity().Any() ) << "a take leaves nothing";
		Inject( 3 );
		EXPECT_EQ( Cues, 2u ) << "and the next thing is told again";
		FlowState after;
		let next = Deployed->TakeActivity( &after );
		EXPECT_EQ( Wires(next, graph), (vector<string>{"in:0>a 1 3", "in:0>b 1 3", "a:0>out 1 \"changed\""}) );
		EXPECT_EQ( after.Counts[3].Received, before.Counts[3].Received+Of(next, 3)->Counts.Received ) << "what a take answers adds to the state the take before gave";
	}

	TEST_F( ActivityTests, AStatusChangeAnErrorAndADropAreActivity ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ), Node( "w", "opc out" ),
			Node( "many", "inject", "{}", {{"parts"}} ), Node( "parts", "split", "{}", {{"out"}} ), Node( "out", "log" )
		}), ETraceLevel::Hops, 3 );
		Deployed->Watch( true );
		Inject( "bad" );
		auto activity = Deployed->TakeActivity();
		auto w = Of( activity, 1 );
		ASSERT_TRUE( w );
		EXPECT_EQ( w->Counts.Errors, 1u );
		EXPECT_EQ( w->Error, "Bad_NotWritable" );
		ASSERT_TRUE( w->Status );
		EXPECT_EQ( *w->Status, (NodeStatus{"red", "ring", "1 uncaught, the last:  Bad_NotWritable"}) );

		Inject( 1 );
		activity = Deployed->TakeActivity();
		w = Of( activity, 1 );
		ASSERT_TRUE( w && w->Status );
		EXPECT_EQ( *w->Status, (NodeStatus{"green", "dot", "Good"}) );
		EXPECT_EQ( w->Counts.Errors, 0u );
		EXPECT_EQ( w->Error, "" );

		Inject( jarray{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, {}, "many" );
		activity = Deployed->TakeActivity();
		let out = Of( activity, 4 );
		ASSERT_TRUE( out );
		EXPECT_EQ( out->Counts.Drops, 7u );
		EXPECT_EQ( out->Counts.Received, 3u );
		ASSERT_TRUE( out->Status );
		EXPECT_EQ( *out->Status, (NodeStatus{"yellow", "ring", "7 dropped"}) );
		ASSERT_EQ( activity.Wires.size(), 2u );
		EXPECT_EQ( activity.Wires[1].Count, 10u ) << "a wire counts what went down it, the queue's drops too";

		EXPECT_FALSE( Of(Deployed->TakeActivity(), 1) ) << "a status that stands is no change";
	}

	TEST_F( ActivityTests, WatchOffKeepsNothing ){
		Run( Nodes({Node("in", "inject", "{}", {{"out"}}), Node("out", "log")}) );
		Deployed->Watch( true );
		Inject( 1 );
		EXPECT_EQ( Cues, 1u );
		Deployed->Watch( false );
		EXPECT_FALSE( Deployed->TakeActivity().Any() ) << "what was kept goes with the watch";
		Inject( 2 );
		EXPECT_EQ( Cues, 1u );
		EXPECT_FALSE( Deployed->TakeActivity().Any() );
		Deployed->Watch( true );
		EXPECT_FALSE( Deployed->TakeActivity().Any() ) << "a watch begins with nothing";
		Inject( 3 );
		EXPECT_EQ( Cues, 2u );
		let activity = Deployed->TakeActivity();
		ASSERT_EQ( activity.Wires.size(), 1u );
		EXPECT_EQ( activity.Wires[0].Count, 1u ) << "and none of the watch before";
	}
}
//The engine (docs/flows/spec.md *Messages*, *Errors and status*):  the clone rule, the queues and their bound, a held
//message, the catch, status and complete nodes, a link, and a flow that does not start.
#include <thread>
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	struct EngineTests : FlowTest{};

	TEST_F( EngineTests, AMessageIsBornAtItsSource ){
		Run( Nodes({Node("in", "inject", R"({"message":{"topic":"shift","payload":{"line":4}}})", {{"out"}}), Node("out", "log")}) );
		Deployed->Inject( "in" );
		Sync();
		let took = Host->Took->Of( "out" );
		ASSERT_EQ( took.size(), 1u );
		EXPECT_EQ( took[0].at("_id"), (uint64_t)duration_cast<seconds>(At(0).time_since_epoch()).count()<<32 | 1 ) << "the run's start and a counter";
		EXPECT_EQ( took[0].at("_time"), Iso(At(6)) ) << "the flow's clock";
		EXPECT_FALSE( took[0].contains("_parent") );
		EXPECT_EQ( took[0].at("topic"), "shift" );
		EXPECT_EQ( serialize(took[0].at("payload")), R"({"line":4})" );

		Advance( 90s );
		jobject override{ {"payload", 5}, {"_id", 1} }, empty;
		Deployed->Inject( "in", &override );
		Deployed->Inject( "in", &empty );
		Sync();
		EXPECT_EQ( Took(), Ƒ(R"({{"line":4}} 5 "{}")", ToIsoString(At(6, 1, 30))) ) << "the mutation's message in place of the node's, and a payload of the time for one with none";
		EXPECT_EQ( Took("out", "topic"), R"("shift" - -)" );
		EXPECT_NE( Host->Took->Of("out")[1].at("_id"), 1 ) << "an id is the engine's";

		EXPECT_THROW( Deployed->Inject("out"), Exception );
		EXPECT_THROW( Deployed->Inject("nowhere"), Exception );
	}

	TEST_F( EngineTests, ASourceEmitsFromAnyThread ){
		Run( Nodes({Node("pump", "opc in", "{}", {{"out"}}), Node("out", "log")}) );
		Host->Pumps["pump"]->Push( 1450, "rpm" );
		Host->Pumps["pump"]->Push( 1460, "rpm" );
		Sync();
		EXPECT_EQ( Took(), "1450 1460" );
		EXPECT_EQ( Counts("pump").Sent, vector<uint>{2} );
		EXPECT_EQ( Counts("out").Received, 2u );
	}

	TEST_F( EngineTests, TheFirstWireTakesTheMessageAndEachOtherAClone ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"a", "b", "c"}} ),
			Node( "a", "change", R"({"rules":[{"t":"set","p":"payload","to":"changed"}]})", {{"outA"}} ),
			Node( "b", "log" ), Node( "c", "log" ), Node( "outA", "log" )
		}) );
		Inject( "original" );
		let a = Host->Took->Of( "outA" ).at( 0 ); let b = Host->Took->Of( "b" ).at( 0 ); let c = Host->Took->Of( "c" ).at( 0 );
		let id = a.at( "_id" );
		EXPECT_FALSE( a.contains("_parent") ) << "the message itself down the first wire";
		EXPECT_EQ( b.at("_parent"), id );
		EXPECT_EQ( c.at("_parent"), id );
		EXPECT_NE( b.at("_id"), id );
		EXPECT_NE( c.at("_id"), b.at("_id") );
		EXPECT_EQ( a.at("payload"), "changed" );
		EXPECT_EQ( b.at("payload"), "original" ) << "a node downstream changes what it holds, and no other's";
		EXPECT_EQ( b.at("_time"), a.at("_time") );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"clone in [0]", "clone in [0]", "birth in [0]*", "hop a [0]*", "hop b []", "hop c []", "hop outA []"}) );
	}

	TEST_F( EngineTests, TheQueuesDrainInArrivalOrder ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"via", "out"}} ),
			Node( "via", "change", R"({"rules":[{"t":"set","p":"topic","to":"via"}]})", {{"out"}} ),
			Node( "out", "log" )
		}) );
		OnStrand( [&]{
			for( let payload : {1, 2} ){
				jobject message{ {"payload", payload}, {"topic", "direct"} };
				Deployed->Inject( "in", &message );
			}
		} );
		EXPECT_EQ( Took(), "1 2 1 2" ) << "each clone reached `out` before what `via` sent after it";
		EXPECT_EQ( Took("out", "topic"), R"("direct" "direct" "via" "via")" );
	}

	TEST_F( EngineTests, AQueuePastItsBoundDropsItsOldest ){
		Run( Nodes({Node("in", "inject", "{}", {{"parts"}}), Node("parts", "split", "{}", {{"out"}}), Node("out", "log")}), ETraceLevel::Hops, 3 );
		Inject( jarray{0, 1, 2, 3, 4, 5, 6, 7, 8, 9} );
		EXPECT_EQ( Took(), "7 8 9" ) << "the split queued ten at `out` before it ran";
		EXPECT_EQ( Counts("out").Drops, 7u );
		EXPECT_EQ( Counts("out").Received, 3u );
		EXPECT_EQ( Counts("parts").Sent, vector<uint>{10} );
		EXPECT_EQ( Deployed->Status("out"), (NodeStatus{"yellow", "ring", "7 dropped"}) ) << "once, when the drain ended";
		EXPECT_EQ( std::ranges::count(Trace->Lines(), "drop out"), 7 );
		EXPECT_EQ( std::ranges::count(Trace->Lines(), "status out"), 1 );

		Inject( jarray{10, 11} );
		EXPECT_EQ( Took(), "7 8 9 10 11" );
		EXPECT_EQ( Counts("out").Drops, 7u );
	}

	TEST_F( EngineTests, AHeldMessageComesBackOnTheStrand ){
		Run( Nodes({Node("in", "inject", "{}", {{"w"}}), Node("w", "opc out", "{}", {{"out"}}), Node("out", "log")}) );
		Inject( "hold" );
		Inject( 5 );
		EXPECT_EQ( Took(), "5" ) << "the node took the next while it held the first";
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "birth in [0]*", "status w", "hop w [0]*", "hop out []"}) ) << "the held hop is still open";
		Host->Takers["w"]->Answer();
		Sync();
		EXPECT_EQ( Took(), R"(5 "hold")" );
		EXPECT_EQ( Trace->Lines().size(), 7u );
		EXPECT_EQ( Trace->Lines()[5], "hop w [0]" );
		EXPECT_EQ( Counts("w").Sent, vector<uint>{2} );
		EXPECT_EQ( Counts("w").Errors, 0u );
	}

	TEST_F( EngineTests, ACatchTakesAnErrorWithItsSourceAndCount ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ),
			Node( "w", "opc out", "{}", {{"out"}} ),
			Node( "caught", "catch", R"({"scope":["w"]})", {{"errors"}} ),
			Node( "others", "catch", R"({"scope":["out"]})", {{"wrong"}} ),
			Node( "rest", "catch", R"({"uncaught":true})", {{"wrong"}} ),
			Node( "out", "log" ), Node( "errors", "log" ), Node( "wrong", "log" )
		}) );
		Inject( "bad", "valve" );
		let took = Host->Took->Of( "errors" );
		ASSERT_EQ( took.size(), 1u );
		EXPECT_EQ( serialize(took[0].at("error")), R"({"message":"Bad_NotWritable","source":{"id":"w","type":"opc out","name":"","count":1}})" );
		EXPECT_EQ( took[0].at("payload"), "bad" );
		EXPECT_EQ( took[0].at("topic"), "valve" );
		EXPECT_TRUE( took[0].contains("_parent") );
		EXPECT_EQ( Took("wrong"), "" ) << "neither a catch of another node nor one for the uncaught";
		EXPECT_EQ( Took(), "" );
		EXPECT_EQ( Counts("w").Errors, 1u );
		EXPECT_EQ( Deployed->Status("w"), NodeStatus{} ) << "a caught error is not the node's status";
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop w []", "error w", "birth caught [0]*", "hop errors []"}) );
		EXPECT_EQ( Trace->Records()[2].Error, "Bad_NotWritable" );
	}

	TEST_F( EngineTests, TheLoopGuardDropsAMessageCaughtNineTimes ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ),
			Node( "w", "opc out" ),
			Node( "again", "catch", "{}", {{"w", "errors"}} ),
			Node( "errors", "log" )
		}) );
		Inject( "bad" );
		EXPECT_EQ( Took("errors", "payload"), R"("bad" "bad" "bad" "bad" "bad" "bad" "bad" "bad" "bad")" );
		vector<int64_t> counts;
		for( let& message : Host->Took->Of("errors") )
			counts.push_back( message.at("error").at("source").at("count").to_number<int64_t>() );
		EXPECT_EQ( counts, (vector<int64_t>{1, 2, 3, 4, 5, 6, 7, 8, 9}) );
		EXPECT_EQ( Counts("w").Errors, 10u ) << "the tenth is not caught";
		EXPECT_EQ( Counts("w").Drops, 1u );
		EXPECT_TRUE( Host->Took->Of("errors")[1].contains("_error") ) << "the error it came with is kept beside the new one";
	}

	TEST_F( EngineTests, WhatNoCatchTakesIsTheNodesStatus ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ),
			Node( "w", "opc out" ),
			Node( "others", "catch", R"({"scope":["in"]})", {{"wrong"}} ),
			Node( "wrong", "log" )
		}) );
		Inject( "bad" );
		Inject( "bad" );
		EXPECT_EQ( Deployed->Status("w"), (NodeStatus{"red", "ring", "2 uncaught, the last:  Bad_NotWritable"}) );
		EXPECT_EQ( Counts("w").Errors, 2u );
		EXPECT_EQ( Took("wrong"), "" );
		Inject( 1 );
		EXPECT_EQ( Deployed->Status("w"), (NodeStatus{"green", "dot", "Good"}) ) << "the node's own status, set since";
	}

	TEST_F( EngineTests, ACatchForTheUncaughtTakesWhatNoOtherDoes ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ),
			Node( "w", "opc out" ),
			Node( "rest", "catch", R"({"uncaught":true})", {{"errors"}} ),
			Node( "errors", "log" )
		}) );
		Inject( "bad" );
		EXPECT_EQ( Took("errors"), R"("bad")" );
		EXPECT_EQ( Deployed->Status("w"), NodeStatus{} );
	}

	TEST_F( EngineTests, AHeldMessageThatFailsReachesTheCatch ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ),
			Node( "w", "opc out", "{}", {{"out"}} ),
			Node( "caught", "catch", "{}", {{"errors"}} ),
			Node( "out", "log" ), Node( "errors", "log" )
		}) );
		Inject( "hold" );
		Host->Takers["w"]->Answer( "Bad_Timeout" );
		Sync();
		EXPECT_EQ( Took("errors", "error"), R"({"message":"Bad_Timeout","source":{"id":"w","type":"opc out","name":"","count":1}})" );
		EXPECT_EQ( Took(), "" );
	}

	TEST_F( EngineTests, AStatusNodeTakesEachChangeWithItsSource ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ),
			Node( "w", "opc out" ),
			Node( "all", "status", "{}", {{"statuses"}} ),
			Node( "other", "status", R"({"scope":["in"]})", {{"wrong"}} ),
			Node( "statuses", "log" ), Node( "wrong", "log" )
		}) );
		Inject( 1 );
		Inject( 2 );
		EXPECT_EQ( Took("statuses", "status"), R"({"fill":"green","shape":"dot","text":"Good","source":{"id":"w","type":"opc out","name":""}})" ) << "a status set again as it stands is no change";
		EXPECT_EQ( Took("wrong"), "" );
		Inject( "bad" );
		EXPECT_EQ( Host->Took->Of("statuses").size(), 2u );
		EXPECT_EQ( Host->Took->Of("statuses")[1].at("status").at("fill"), "red" );
	}

	TEST_F( EngineTests, ACompleteNodeTakesWhatItsNodesFinish ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"w"}} ),
			Node( "w", "opc out", "{}", {{"out"}} ),
			Node( "done", "complete", R"({"scope":["w", "out"]})", {{"finished"}} ),
			Node( "out", "log" ), Node( "finished", "log" )
		}) );
		Inject( 7 );
		let took = Host->Took->Of( "finished" );
		ASSERT_EQ( took.size(), 2u ) << "the write's, then the log's, which sent nothing on";
		let id = Host->Took->Of( "out" )[0].at( "_id" );
		EXPECT_EQ( took[0].at("_parent"), id );
		EXPECT_EQ( took[0].at("status"), 0 ) << "as the node sent it";
		EXPECT_EQ( took[1].at("_parent"), id );
		Inject( "bad" );
		EXPECT_EQ( Host->Took->Of("finished").size(), 2u ) << "an error is not a finish";
		Inject( "hold" );
		EXPECT_EQ( Host->Took->Of("finished").size(), 2u ) << "nor is a message the node still holds";
		Host->Takers["w"]->Answer();
		Sync();
		EXPECT_EQ( Host->Took->Of("finished").size(), 4u );
	}

	TEST_F( EngineTests, ALinkJoinsTwoPartsOfAFlow ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"to"}} ),
			Node( "to", "link out", R"({"name":"alerts"})" ),
			Node( "from", "link in", R"({"name":"alerts"})", {{"out"}} ),
			Node( "out", "log" )
		}) );
		Inject( "over" );
		EXPECT_EQ( Took(), R"("over")" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop to [0]", "hop from [0]", "hop out []"}) );
	}

	TEST_F( EngineTests, ALinkToAnotherFlowIsItsHosts ){
		let nodes = Nodes( {Node("in", "inject", "{}", {{"to"}}), Node("to", "link out", R"({"name":"elsewhere"})")} );
		auto graph = Graph::Read( nodes, true );
		ASSERT_TRUE( graph.has_value() ) << graph.error().Message;
		Deployed.emplace( move(*graph), Flow::Config{.Name="linked", .Clock=Time, .Host=Host} );
		ASSERT_TRUE( Deployed->Start() );
		Inject( "over" );
		EXPECT_EQ( Took("to"), R"("over")" ) << "the host's sink takes what the link sends";

		Flow none{ *Graph::Read(nodes, true), {.Name="unhosted", .Clock=Time} };
		EXPECT_FALSE( none.Start() );
		EXPECT_TRUE( Has(none.Fault()->Message, "No `link in` of the flow is named 'elsewhere'") );
	}

	TEST_F( EngineTests, ALinkFromAnotherFlowIsBornAtItsLinkIn ){
		Run( Nodes({Node("from", "link in", R"({"name":"alerts"})", {{"out"}}), Node("out", "log")}) );
		const Message sent{ jobject{{"_id", 7}, {"payload", "over"}, {"topic", "t"}} };
		EXPECT_TRUE( Deployed->Link("alerts", sent) );
		Sync();
		let took = Host->Took->Of( "out" );
		ASSERT_EQ( took.size(), 1u );
		EXPECT_EQ( took[0].at("payload"), "over" );
		EXPECT_EQ( took[0].at("_parent"), 7 ) << "a clone, the sender's message its parent";
		EXPECT_NE( took[0].at("_id"), 7 );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth from [0]*", "hop out []"}) );
		EXPECT_EQ( Counts("from").Sent, vector<uint>{1} );

		EXPECT_FALSE( Deployed->Link("elsewhere", sent) ) << "no `link in` of the name";
		Stop();
		EXPECT_FALSE( Deployed->Link("alerts", sent) ) << "a flow that is not running takes none";
	}

	//What the queue's bound would drop, sent in one go, a source that waits for the flow between pages gets through whole.
	TEST_F( EngineTests, AReadingSourceKeepsWithinTheQueues ){
		Run( Nodes({
			Node( "pump", "opc in", "{}", {{"a"}} ),
			Node( "a", "change", R"({"rules":[{"t":"set","p":"seen","to":true}]})", {{"b", "out"}} ),
			Node( "b", "change", R"({"rules":[{"t":"set","p":"seen","to":true}]})", {{"out"}} ),
			Node( "out", "log" )
		}), ETraceLevel::Counts, 10 );
		constexpr uint count{ 2500 };
		Host->Pumps["pump"]->Read( count, 5 );
		for( uint i=0; Host->Took->Of("out").size()<2*count && i<5000; ++i )
			std::this_thread::sleep_for( 1ms );
		Sync();
		EXPECT_EQ( Host->Took->Of("out").size(), 2*count ) << "each value twice, by the wire and by its clone through b";
		for( let node : {"pump", "a", "b", "out"} )
			EXPECT_EQ( Counts(node).Drops, 0u ) << node;
	}

	//reviews-issue/449-flows-sources-sinks.md #1, #4:  a held message is not through the flow, so a source that waits on Idle
	//for its next page waits for each one it sent to be answered.
	TEST_F( EngineTests, AReadingSourceWaitsForWhatIsHeld ){
		Run( Nodes({Node("pump", "opc in", "{}", {{"w"}}), Node("w", "opc out")}) );
		Host->Pumps["pump"]->Read( 4, 2, "hold" );
		Sync();
		EXPECT_EQ( Counts("pump").Sent, vector<uint>{2} ) << "the first page, held";
		Host->Takers["w"]->Answer();
		Sync();
		EXPECT_EQ( Counts("pump").Sent, vector<uint>{2} ) << "one is held still";
		Host->Takers["w"]->Answer();
		Sync();
		EXPECT_EQ( Counts("pump").Sent, vector<uint>{4} ) << "the second, once neither is";
	}

	TEST_F( EngineTests, StartTellsWhenItsNodesHaveStarted ){
		auto graph = Graph::Read( Nodes({Node("in", "inject", "{}", {{"v"}}), Node("v", "variable")}) );
		ASSERT_TRUE( graph.has_value() );
		Flow failing{ move(*graph), {.Name="unstarted", .Clock=Time, .Host=Host} };
		std::promise<bool> told;
		EXPECT_TRUE( failing.Start([&]{ told.set_value( failing.Running() ); }) ) << "its nodes were made";
		EXPECT_FALSE( told.get_future().get() ) << "told once the node's Start has thrown";
		EXPECT_EQ( failing.Fault()->NodeId, "v" );

		Flow running{ *Graph::Read(Nodes({Node("in", "inject", "{}", {{"out"}}), Node("out", "log")})), {.Name="started", .Clock=Time, .Host=Host} };
		std::promise<bool> started;
		EXPECT_TRUE( running.Start([&]{ started.set_value( running.Running() ); }) );
		EXPECT_TRUE( started.get_future().get() );

		Flow unhosted{ *Graph::Read(Nodes({Node("out", "log")})), {.Name="unhosted", .Clock=Time} };
		bool here{};
		EXPECT_FALSE( unhosted.Start([&]{ here = true; }) );
		EXPECT_TRUE( here ) << "a flow whose nodes could not be made is told at once";
	}

	TEST_F( EngineTests, AHostStopsAFlowForANode ){
		auto& flow = Run( Nodes({Node("in", "inject", "{}", {{"w"}}), Node("w", "opc out", "{}", {{"out"}}), Node("out", "log")}) );
		Inject( "hold" );
		std::promise<void> done;
		flow.Stop( {"w", "Update on the group was withdrawn."}, [&]{ done.set_value(); } );
		done.get_future().get();
		EXPECT_FALSE( flow.Running() );
		ASSERT_TRUE( flow.Fault().has_value() );
		EXPECT_EQ( flow.Fault()->NodeId, "w" );
		EXPECT_EQ( flow.Status("w"), (NodeStatus{"red", "ring", "Update on the group was withdrawn."}) );
		EXPECT_EQ( Counts("w").Drops, 1u ) << "what the node held is dropped, as any Stop drops it";
		flow.Stop( {"out", "later"} );
		EXPECT_EQ( flow.Fault()->NodeId, "w" ) << "a flow that has stopped keeps the fault it has";

		Flow idle{ *Graph::Read(Nodes({Node("out", "log")})), {.Name="idle", .Clock=Time, .Host=Host} };
		idle.Stop( {"out", "The group is gone."} );
		EXPECT_EQ( idle.Fault()->Message, "The group is gone." ) << "a flow that never started takes it too";
		EXPECT_EQ( idle.Status("out"), (NodeStatus{"red", "ring", "The group is gone."}) );
		EXPECT_FALSE( idle.Start() );

		Flow whole{ *Graph::Read(Nodes({Node("out", "log")})), {.Name="whole", .Clock=Time, .Host=Host} };
		whole.Stop( {"", "Its deployer is gone."} );
		EXPECT_EQ( whole.Fault()->NodeId, "" ) << "a fault of no node is the flow's own";
		EXPECT_EQ( whole.Fault()->Message, "Its deployer is gone." );
	}

	TEST_F( EngineTests, AFlowWhoseSourceIsRefusedStaysStopped ){
		auto graph = Graph::Read( Nodes({Node("in", "inject", "{}", {{"out"}}), Node("history", "group in", R"({"group":"pumps"})", {{"out"}}), Node("out", "log")}) );
		ASSERT_TRUE( graph.has_value() );
		Flow flow{ move(*graph), {.Name="refused", .Clock=Time, .Host=Host} };
		EXPECT_FALSE( flow.Start() );
		EXPECT_FALSE( flow.Running() );
		let fault = flow.Fault();
		ASSERT_TRUE( fault.has_value() );
		EXPECT_EQ( fault->NodeId, "history" );
		EXPECT_EQ( fault->Message, "The gateway has no historian." );
		EXPECT_EQ( flow.Status("history"), (NodeStatus{"red", "ring", "The gateway has no historian."}) );
		EXPECT_THROW( flow.Inject("in"), Exception );
		EXPECT_FALSE( flow.Start() ) << "a flow runs once";

		auto unhosted = Graph::Read( Nodes({Node("out", "log")}) );
		Flow none{ move(*unhosted), {.Name="unhosted", .Clock=Time} };
		EXPECT_FALSE( none.Start() );
		EXPECT_TRUE( Has(none.Fault()->Message, "Nothing here makes a 'log' node") );
	}

	TEST_F( EngineTests, AStopWhileTheNodesAreMadeWins ){
		auto graph = Graph::Read( Nodes({Node("pump", "opc in", "{}", {{"out"}}), Node("out", "log")}) );
		ASSERT_TRUE( graph.has_value() );
		Flow flow{ move(*graph), {.Name="stopped", .Clock=Time, .Host=Host} };
		bool stopped{};
		Host->OnSource = [&]{ flow.Stop( [&]{ stopped = true; } ); };
		EXPECT_FALSE( flow.Start() );
		EXPECT_TRUE( stopped ) << "a flow that never started stops at once";
		EXPECT_FALSE( flow.Running() );
		EXPECT_FALSE( flow.Fault().has_value() );
		Host->OnSource = {};
		EXPECT_FALSE( flow.Start() ) << "a flow runs once";
	}

	TEST_F( EngineTests, ANodeWhoseStartThrowsStopsTheFlow ){
		auto graph = Graph::Read( Nodes({Node("in", "inject", "{}", {{"v"}}), Node("v", "variable")}) );
		ASSERT_TRUE( graph.has_value() );
		Flow flow{ move(*graph), {.Name="unstarted", .Clock=Time, .Host=Host} };
		EXPECT_TRUE( flow.Start() ) << "its nodes were made";
		for( uint i=0; flow.Running() && i<1000; ++i )
			std::this_thread::sleep_for( 1ms );
		EXPECT_FALSE( flow.Running() );
		EXPECT_EQ( flow.Fault()->NodeId, "v" );
		EXPECT_EQ( flow.Fault()->Message, "The flow server is not listening." );
	}

	TEST_F( EngineTests, ATimerOnTheSystemClockComesBackToTheStrand ){
		auto graph = Graph::Read( Nodes({Node("in", "inject", "{}", {{"wait"}}), Node("wait", "delay", R"({"duration":"PT0.02S"})", {{"out"}}), Node("out", "log")}) );
		ASSERT_TRUE( graph.has_value() );
		Flow flow{ move(*graph), {.Name="live", .Host=Host} };
		ASSERT_TRUE( flow.Start() );
		for( let payload : {1, 2, 3} ){
			jobject message{ {"payload", payload} };
			flow.Inject( "in", &message );
		}
		for( uint i=0; Host->Took->Of("out").size()<3 && i<2000; ++i )
			std::this_thread::sleep_for( 1ms );
		EXPECT_EQ( Took(), "1 2 3" );
		std::promise<void> stopped;
		flow.Stop( [&]{ stopped.set_value(); } );
		stopped.get_future().get();
	}

	TEST_F( EngineTests, AStopDropsWhatIsHeld ){
		Run( Nodes({Node("in", "inject", "{}", {{"w"}}), Node("w", "opc out", "{}", {{"out"}}), Node("out", "log")}) );
		Inject( "hold" );
		Inject( "hold" );
		Stop();
		EXPECT_FALSE( Deployed->Running() );
		EXPECT_EQ( Counts("w").Drops, 2u ) << "counted on the trace of the flow that stopped";
		EXPECT_EQ( std::ranges::count(Trace->Lines(), "drop w"), 2 );
		EXPECT_THROW( Deployed->Inject("in"), Exception );
		Host->Takers["w"]->Answer();//an answer that comes after the stop goes nowhere.
		Stop();
		EXPECT_EQ( Took(), "" );
	}

	TEST_F( EngineTests, ASecondStopRunsBehindTheFirstsTeardown ){
		Run( Nodes({Node("in", "inject", "{}", {{"w"}}), Node("w", "opc out")}) );
		Inject( "hold" );
		std::promise<uint> second;
		Posted( [&]{//on the strand, so the first Stop's teardown waits behind this.
			Deployed->Stop();
			Deployed->Stop( [&]{ second.set_value(Counts("w").Drops); } );
		} );
		EXPECT_EQ( second.get_future().get(), 1u ) << "the held write was dropped first";
	}

	TEST_F( EngineTests, AFlowAssignedOverIsStopped ){
		Run( Nodes({Node("in", "inject", "{}", {{"w"}}), Node("w", "opc out")}) );
		Inject( "hold" );
		auto next = Graph::Read( Nodes({Node("out", "log")}) );
		ASSERT_TRUE( next.has_value() );
		let trace = Trace;
		*Deployed = Flow{ move(*next), {.Name="next", .Clock=Time, .Host=Host} };
		for( uint i=0; std::ranges::count(trace->Lines(), "drop w")==0 && i<1000; ++i )
			std::this_thread::sleep_for( 1ms );
		EXPECT_EQ( std::ranges::count(trace->Lines(), "drop w"), 1 ) << "the flow replaced was torn down";
	}

	TEST_F( EngineTests, ADrainGivesTheStrandItsTurn ){
		Run( Nodes({Node("pump", "opc in", "{}", {{"out"}}), Node("out", "log")}), ETraceLevel::Counts, 5000 );
		constexpr uint count{ 2500 };
		optional<uint> seen;
		Posted( [&]{
			for( uint i=0; i<count; ++i )
				Host->Pumps["pump"]->Push( jvalue(i) );
			Deployed->Post( [&]{ seen = Host->Took->Of("out").size(); } );
		} );
		for( uint i=0; Host->Took->Of("out").size()<count && i<2000; ++i )
			std::this_thread::sleep_for( 1ms );
		Sync();
		ASSERT_TRUE( seen.has_value() );
		EXPECT_LT( *seen, count ) << "the post ran between two turns of the drain";
		EXPECT_EQ( Host->Took->Of("out").size(), count );
	}

	TEST_F( EngineTests, APostThatThrowsIsItsNodesError ){
		Run( Nodes({Node("in", "inject", "{}", {{"w"}}), Node("w", "opc out")}) );
		Inject( "hold" );
		Host->Takers["w"]->Garble();
		Sync();
		EXPECT_EQ( Counts("w").Errors, 1u );
		EXPECT_TRUE( Has(Deployed->Status("w").Text, "1 uncaught, the last:  The answer does not read.") ) << Deployed->Status("w").Text;
		EXPECT_EQ( std::ranges::count(Trace->Lines(), "error w"), 1 ) << "the trace has the error that turned it red";
	}
}
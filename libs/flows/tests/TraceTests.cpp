//The trace (docs/flows/spec.md *Trace*):  the hooks folded to one record per node per message, what each level keeps, and
//the minute figures the engine gives whoever asks.
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	struct TraceTests : FlowTest{
		//An inject through a change, which marks the message, into a switch:  "bad" to a write that refuses it, the rest to a log.
		α Route( ETraceLevel level )->void{
			Run( Nodes({
				Node( "in", "inject", "{}", {{"mark"}} ),
				Node( "mark", "change", R"({"rules":[{"t":"set","p":"seen","to":true}]})", {{"sw"}} ),
				Node( "sw", "switch", R"({"rules":[{"t":"eq","v":"bad"},{"t":"else"}]})", {{"w"}, {"out"}} ),
				Node( "w", "opc out" ), Node( "out", "log" )
			}), level );
		}
	};

	TEST_F( TraceTests, AHopIsOneRecordPerNodePerMessage ){
		Route( ETraceLevel::Hops );
		Advance( 30s );
		Inject( 7, "rpm" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [1]", "hop out []"}) );
		let records = Trace->Records();
		let id = Host->Took->Of( "out" )[0].at( "_id" ).to_number<MessageId>();
		for( let& record : records ){
			EXPECT_EQ( record.Id, id ) << record.Node;
			EXPECT_EQ( record.Parent, 0u );
			EXPECT_EQ( record.Time, At(6, 0, 30) );
		}
		EXPECT_EQ( parse(*records[0].Json).at("payload"), 7 ) << "a birth carries the message";
		EXPECT_EQ( parse(*records[1].Json).at("seen"), true ) << "a hop carries it where the node changed it";
		EXPECT_FALSE( records[2].Json ) << "and not where it passed as it came";
		EXPECT_EQ( records[2].Ports, vector<uint>{1} );
	}

	TEST_F( TraceTests, HopsKeepsEverything ){
		Route( ETraceLevel::Hops );
		Inject( "bad" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [0]", "hop w []", "error w", "status w"}) );
		let records = Trace->Records();
		EXPECT_EQ( records[4].Error, "Bad_NotWritable" );
		EXPECT_EQ( records[4].Id, records[0].Id );
		EXPECT_EQ( *records[5].Status, (NodeStatus{"red", "ring", "1 uncaught, the last:  Bad_NotWritable"}) );
	}

	TEST_F( TraceTests, ErrorsKeepsThePathOfAMessageThatFails ){
		Route( ETraceLevel::Errors );
		Inject( 7 );
		Inject( 8 );
		EXPECT_EQ( Trace->Lines(), vector<string>{} ) << "a quiet flow writes nothing";
		Inject( "bad" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [0]", "hop w []", "error w", "status w"}) ) << "its whole path, at its end";
		Inject( 9 );
		EXPECT_EQ( Trace->Lines().size(), 6u );
	}

	TEST_F( TraceTests, ErrorsKeepsWhatAFailedCloneWasMadeFrom ){
		Run( Nodes({Node("in", "inject", "{}", {{"out", "w"}}), Node("w", "opc out"), Node("out", "log")}), ETraceLevel::Errors );
		Inject( "bad" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop out []", "clone in [0]", "hop w []", "error w", "status w"}) );
		let records = Trace->Records();
		EXPECT_EQ( records[2].Parent, records[0].Id );
		EXPECT_EQ( records[4].Id, records[2].Id );
		EXPECT_EQ( records[4].Parent, records[0].Id );
	}

	TEST_F( TraceTests, ErrorsKeepsDrops ){
		Run( Nodes({Node("in", "inject", "{}", {{"parts"}}), Node("parts", "split", "{}", {{"out"}}), Node("out", "log")}), ETraceLevel::Errors, 2 );
		Inject( jarray{1, 2, 3} );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"drop out", "status out"}) );
	}

	TEST_F( TraceTests, CountsAndOffWriteNothing ){
		for( let level : {ETraceLevel::Counts, ETraceLevel::Off} ){
			Route( level );
			Inject( 7 );
			Inject( "bad" );
			EXPECT_EQ( Trace->Lines(), vector<string>{} ) << ToString( level );
			EXPECT_EQ( Counts("w").Errors, 1u ) << "the figures are the engine's, whatever the level";
			Stop();
			Host = ms<TestHost>();
		}
		EXPECT_EQ( ToTraceLevel("errors"), ETraceLevel::Errors );
		EXPECT_EQ( ToString(ETraceLevel::Hops), "hops" );
		EXPECT_FALSE( ToTraceLevel("all") );
	}

	TEST_F( TraceTests, TheEngineKeepsTheMinuteFigures ){
		Route( ETraceLevel::Off );
		Advance( 30s );
		Inject( 7 );
		Inject( 8 );
		Inject( "bad" );
		EXPECT_TRUE( Deployed->TakeMinutes().empty() ) << "the minute has not ended";
		Advance( 40s );
		Inject( 9 );
		Advance( 10s );
		auto minutes = Deployed->TakeMinutes();
		ASSERT_EQ( minutes.size(), 1u ) << "06:01 is still open";
		EXPECT_EQ( minutes[0].Minute, At(6) );
		auto figures = []( const MinuteCounts& minute ){
			string y;
			for( let& node : minute.Nodes ){
				string sent;
				for( let count : node.Sent )
					sent += Ƒ( "{}{}", sent.empty() ? "" : ",", count );
				y += Ƒ( "{}{} {} [{}] {} {}", y.empty() ? "" : " | ", node.Node, node.Received, sent, node.Drops, node.Errors );
			}
			return y;
		};
		EXPECT_EQ( figures(minutes[0]), "in 0 [3] 0 0 | mark 3 [3] 0 0 | sw 3 [1,2] 0 0 | w 1 [] 0 1 | out 2 [] 0 0" ) << "node received [sent per port] drops errors";
		EXPECT_TRUE( Deployed->TakeMinutes().empty() ) << "a minute is taken once";
		Advance( 3min );
		minutes = Deployed->TakeMinutes();
		ASSERT_EQ( minutes.size(), 1u ) << "a minute with no figure is not one";
		EXPECT_EQ( minutes[0].Minute, At(6, 1) );
		EXPECT_EQ( figures(minutes[0]), "in 0 [1] 0 0 | mark 1 [1] 0 0 | sw 1 [0,1] 0 0 | out 1 [] 0 0" );
		EXPECT_EQ( Counts("sw").Sent, (vector<uint>{1, 3}) ) << "and the totals since the start";
		EXPECT_EQ( Counts("in").Sent, vector<uint>{4} );
	}
}
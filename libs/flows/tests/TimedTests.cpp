//The nodes that run on the flow's clock (docs/flows/spec.md *Nodes*), a ManualClock here:  delay, trigger, window and a
//join's timeout, none taking wall-clock time.
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	struct TimedTests : FlowTest{
		α One( sv type, sv props, uint queue=1000 )->void{
			Run( Nodes({
				Node( "in", "inject", "{}", {{"n"}} ), Node( "n", type, props, {{"out"}} ), Node( "out", "log" ),
				Node( "caught", "catch", "{}", {{"errors"}} ), Node( "errors", "log" )
			}), ETraceLevel::Hops, queue );
		}
		α Last()->jobject{
			let took = Host->Took->Of( "out" );
			if( took.empty() ){
				ADD_FAILURE() << "out took nothing";
				return {};
			}
			return took.back();
		}
		//A window's figures, `count sum min max mean first last delta`.
		Ω Figures( const jobject& message )->string{
			string y;
			for( let key : {"count", "sum", "min", "max", "mean", "first", "last", "delta"} )
				y += Ƒ( "{}{}", y.empty() ? "" : " ", message.at("payload").at(key).to_number<double>() );
			return y;
		}
	};

	TEST_F( TimedTests, DelayHoldsEachMessage ){
		One( "delay", R"({"duration":"PT5S"})" );
		Inject( 1 );
		Advance( 2s );
		Inject( 2 );
		Advance( 2999ms );
		EXPECT_EQ( Took(), "" );
		EXPECT_EQ( Counts("n").Received, 2u );
		Advance( 1ms );
		EXPECT_EQ( Took(), "1" );
		EXPECT_EQ( Last().at("_time"), Iso(At(6)) ) << "the time its source made it";
		Advance( 2s );
		EXPECT_EQ( Took(), "1 2" );
		EXPECT_EQ( Counts("n").Sent, vector<uint>{2} );
		EXPECT_EQ( Time->Pending(), 1u ) << "the minute's end alone";
	}

	TEST_F( TimedTests, DelayLimitsARate ){
		One( "delay", R"({"mode":"rate","count":2,"per":"PT1S"})" );
		for( let payload : {1, 2, 3} )
			Inject( payload );
		EXPECT_EQ( Took(), "1" ) << "the first goes by, the rest wait their turn";
		Advance( 499ms );
		EXPECT_EQ( Took(), "1" );
		Advance( 1ms );
		EXPECT_EQ( Took(), "1 2" );
		Advance( 500ms );
		EXPECT_EQ( Took(), "1 2 3" );
		Advance( 10s );
		Inject( 4 );
		EXPECT_EQ( Took(), "1 2 3 4" ) << "nothing waits, so it goes by";
	}

	TEST_F( TimedTests, DelayDropsPastARate ){
		One( "delay", R"({"mode":"rate","per":"PT1S","drop":true})" );
		for( let payload : {1, 2, 3} )
			Inject( payload );
		Advance( 1s );
		Inject( 4 );
		Advance( 10s );
		EXPECT_EQ( Took(), "1 4" );
		EXPECT_EQ( Counts("n").Drops, 0u ) << "ended by the node's rule, not lost by a queue";
	}

	TEST_F( TimedTests, WhatWaitsOnARateIsBounded ){
		One( "delay", R"({"mode":"rate","per":"PT1S"})", 2 );
		for( let payload : {1, 2, 3, 4, 5} )
			Inject( payload );
		EXPECT_EQ( Counts("n").Drops, 2u );
		EXPECT_EQ( Deployed->Status("n"), (NodeStatus{"yellow", "ring", "2 dropped"}) );
		Advance( 2s );
		EXPECT_EQ( Took(), "1 4 5" ) << "the oldest went";
	}

	TEST_F( TimedTests, WhatADelayHoldsIsBounded ){
		One( "delay", R"({"duration":"PT5S"})", 2 );
		for( let payload : {1, 2, 3} ){
			Inject( payload );
			Advance( 1s );
		}
		EXPECT_EQ( Counts("n").Drops, 1u );
		EXPECT_EQ( Time->Pending(), 3u ) << "the oldest's timer went with it;  two, and the minute's end";
		Advance( 2999ms );
		EXPECT_EQ( Took(), "" );
		Advance( 1ms );
		EXPECT_EQ( Took(), "2" ) << "five seconds from when it came";
		Advance( 1s );
		EXPECT_EQ( Took(), "2 3" );
	}

	TEST_F( TimedTests, TriggerSendsAValueAndAnotherAfterAWait ){
		One( "trigger", R"({"first":"on","second":"off","wait":"PT10S"})" );
		Inject( 1, "valve" );
		EXPECT_EQ( Took(), R"("on")" );
		let first = Last();
		Advance( 5s );
		Inject( 2, "valve" );
		EXPECT_EQ( Took(), R"("on")" ) << "armed, so it ends at the trigger";
		Advance( 4999ms );
		EXPECT_EQ( Took(), R"("on")" );
		Advance( 1ms );
		EXPECT_EQ( Took(), R"("on" "off")" ) << "the wait is not extended";
		EXPECT_EQ( Last().at("_parent"), first.at("_id") );
		EXPECT_EQ( Last().at("topic"), "valve" );
		EXPECT_EQ( Last().at("_time"), Iso(At(6, 0, 10)) );
		Inject( 3 );
		EXPECT_EQ( Took(), R"("on" "off" "on")" ) << "armed again";
	}

	TEST_F( TimedTests, TriggerIsTheWatchdog ){
		One( "trigger", R"({"second":"no value for 30 s","wait":"PT30S","extend":true})" );
		for( uint i=0; i<3; ++i ){
			Inject( 1450 );
			Advance( 10s );
		}
		Advance( 19s );
		EXPECT_EQ( Took(), "" ) << "each value starts the wait again";
		Advance( 1s );
		EXPECT_EQ( Took(), R"("no value for 30 s")" );
		EXPECT_EQ( Last().at("_time"), Iso(At(6, 0, 50)) );
		Advance( 5min );
		EXPECT_EQ( Host->Took->Of("out").size(), 1u ) << "once, until a value arms it again";
		Inject( 1450 );
		Advance( 30s );
		EXPECT_EQ( Host->Took->Of("out").size(), 2u );
	}

	TEST_F( TimedTests, TriggerResetsAndArmsPerTopic ){
		One( "trigger", R"({"first":1,"second":0,"wait":"PT10S","reset":"stop"})" );
		Inject( "go" );
		Inject( "stop" );
		Advance( 20s );
		EXPECT_EQ( Took(), "1" ) << "a reset disarms it, and sends nothing";
		Inject( "go" );
		jobject reset{ {"payload", "anything"}, {"reset", true} };
		Deployed->Inject( "in", &reset );
		Advance( 20s );
		EXPECT_EQ( Took(), "1 1" );
		EXPECT_EQ( Time->Pending(), 1u ) << "the minute's end alone";
		Stop();
		Host = ms<TestHost>();
		One( "trigger", R"({"second":"late","wait":"PT10S","perTopic":true})" );
		Inject( 1, "a" );
		Advance( 5s );
		Inject( 1, "b" );
		Advance( 5s );
		EXPECT_EQ( Took("out", "topic"), R"("a")" );
		Advance( 5s );
		EXPECT_EQ( Took("out", "topic"), R"("a" "b")" );
	}

	TEST_F( TimedTests, WindowTumblesOnTheClock ){
		One( "window", R"({"duration":"PT1M"})" );
		Advance( 10s );
		Inject( 10, "rpm" );
		Advance( 10s );
		Inject( 20, "rpm" );
		Inject( 5, "temp" );
		let last = Counts( "n" ).Received;
		Advance( 39s );
		EXPECT_EQ( Took(), "" );
		Advance( 1s );
		let took = Host->Took->Of( "out" );
		ASSERT_EQ( took.size(), 2u ) << "a window per topic, at the minute";
		EXPECT_EQ( took[0].at("topic"), "rpm" );
		EXPECT_EQ( Figures(took[0]), "2 30 10 20 15 10 20 10" );
		EXPECT_EQ( took[0].at("_time"), Iso(At(6, 1)) );
		EXPECT_TRUE( took[0].contains("_parent") ) << "the last message in the window";
		EXPECT_EQ( took[1].at("topic"), "temp" );
		EXPECT_EQ( Figures(took[1]), "1 5 5 5 5 5 5 0" );
		EXPECT_EQ( last, 3u );
		Advance( 5min );
		EXPECT_EQ( Host->Took->Of("out").size(), 2u ) << "an empty window sends nothing";
		EXPECT_EQ( Time->Pending(), 0u );
		Advance( 30s );
		Inject( 7, "rpm" );
		Advance( 30s );
		EXPECT_EQ( Figures(Last()), "1 7 7 7 7 7 7 0" );
		EXPECT_EQ( Last().at("_time"), Iso(At(6, 7)) );
	}

	TEST_F( TimedTests, WindowByCountAndSliding ){
		One( "window", R"({"count":3,"perTopic":false})" );
		for( let payload : {1, 2, 3, 4, 5} )
			Inject( payload, payload%2 ? "odd" : "even" );
		ASSERT_EQ( Host->Took->Of("out").size(), 1u );
		EXPECT_EQ( Figures(Last()), "3 6 1 3 2 1 3 2" );
		EXPECT_FALSE( Last().contains("topic") );
		Stop();
		Host = ms<TestHost>();
		One( "window", R"({"kind":"sliding","count":2})" );
		string sums;
		for( let payload : {1, 2, 5} ){
			Inject( payload );
			sums += Figures( Last() ).substr( 0, 4 );
		}
		EXPECT_EQ( sums, "1 1 2 3 2 7 " ) << "the count and the sum of the last two, at each value";
		Stop();
		Host = ms<TestHost>();
		One( "window", R"({"kind":"sliding","duration":"PT10S"})" );
		Inject( 1 );
		Advance( 5s );
		Inject( 2 );
		Advance( 6s );
		Inject( 4 );
		EXPECT_EQ( Figures(Last()), "2 6 2 4 3 2 4 2" ) << "the first is more than ten seconds back";
	}

	TEST_F( TimedTests, WindowResetsAndRefusesWhatIsNotANumber ){
		One( "window", R"({"duration":"PT1M"})" );
		Inject( 10 );
		jobject reset{ {"payload", 0}, {"reset", true} };
		Deployed->Inject( "in", &reset );
		Advance( 2min );
		EXPECT_EQ( Took(), "" );
		EXPECT_EQ( Time->Pending(), 0u );
		Inject( "fast" );
		EXPECT_EQ( Took("errors", "error"), R"({"message":"'payload' is not a number to take into a window.","source":{"id":"n","type":"window","name":"","count":1}})" );
	}

	TEST_F( TimedTests, JoinGoesOnItsTimeout ){
		One( "join", R"({"mode":"array","timeout":"PT5S"})" );
		Inject( 1, "a" );
		Advance( 2s );
		Inject( 2, "b" );
		Advance( 2999ms );
		EXPECT_EQ( Took(), "" );
		Advance( 1ms );
		EXPECT_EQ( Took(), "[1,2]" ) << "five seconds from its first message";
		EXPECT_EQ( Last().at("topic"), "b" );
		Advance( 1min );
		EXPECT_EQ( Host->Took->Of("out").size(), 1u );
	}

	TEST_F( TimedTests, AStopCancelsTheTimers ){
		One( "delay", R"({"duration":"PT5S"})" );
		Inject( 1 );
		EXPECT_EQ( Time->Pending(), 2u ) << "the delay's, and the minute's end";
		Stop();
		EXPECT_EQ( Time->Pending(), 0u );
		EXPECT_EQ( Counts("n").Drops, 1u ) << "what the delay held";
		Time->Advance( 10s );
		EXPECT_EQ( Took(), "" );
	}
}
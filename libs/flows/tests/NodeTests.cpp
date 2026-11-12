//Each stock node over scripted messages (docs/flows/spec.md *Nodes*);  those that run on the clock are in TimedTests.cpp.
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	struct NodeTests : FlowTest{
		//An inject into one node of `type` into a log, with a catch for what the node refuses.
		α One( sv type, sv props )->void{
			Run( Nodes({
				Node( "in", "inject", "{}", {{"n"}} ), Node( "n", type, props, {{"out"}} ), Node( "out", "log" ),
				Node( "caught", "catch", "{}", {{"errors"}} ), Node( "errors", "log" )
			}) );
		}
		//A switch of `rules` with a log per output, `o0` on.
		α Switch( sv props, uint outputs )->void{
			jarray nodes{ Node("in", "inject", "{}", {{"sw"}}) };
			jarray wires;
			for( uint i=0; i<outputs; ++i ){
				nodes.push_back( Node(Ƒ("o{}", i), "log") );
				wires.push_back( jarray{Ƒ("o{}", i)} );
			}
			auto sw = Node( "sw", "switch", props );
			sw["wires"] = move( wires );
			nodes.push_back( move(sw) );
			_outputs = outputs;
			Run( jobject{{"nodes", move(nodes)}} );
		}
		//The outputs that took the payload, `0 2`.
		α Ports( jvalue payload )->string{
			jobject message{ {"payload", move(payload)} };
			vector<uint> before;
			for( uint i=0; i<_outputs; ++i )
				before.push_back( Host->Took->Of(Ƒ("o{}", i)).size() );
			Deployed->Inject( "in", &message );
			Sync();
			string y;
			for( uint i=0; i<_outputs; ++i ){
				if( Host->Took->Of(Ƒ("o{}", i)).size()>before[i] )
					y += Ƒ( "{}{}", y.empty() ? "" : " ", i );
			}
			return y;
		}
		α Last( sv node="out" )->jobject{
			let took = Host->Took->Of( node );
			if( took.empty() ){
				ADD_FAILURE() << node << " took nothing";
				return {};
			}
			return took.back();
		}
		α Errors()->string{//each caught error's message.
			string y;
			for( let& message : Host->Took->Of("errors") )
				y += Ƒ( "{}{}", y.empty() ? "" : " | ", sv{message.at("error").at("message").as_string()} );
			return y;
		}
	private:
		uint _outputs{};
	};

	TEST_F( NodeTests, SwitchCompares ){
		Switch( R"({"rules":[{"t":"eq","v":5},{"t":"neq","v":5},{"t":"lt","v":5},{"t":"lte","v":5},{"t":"gt","v":5},{"t":"gte","v":5},{"t":"btwn","v":1,"v2":10}]})", 7 );
		EXPECT_EQ( Ports(5), "0 3 5 6" );
		EXPECT_EQ( Ports(5.0), "0 3 5 6" ) << "numbers by value, whatever their kinds";
		EXPECT_EQ( Ports(3), "1 2 3 6" );
		EXPECT_EQ( Ports(12.5), "1 4 5" );
		EXPECT_EQ( Ports("5"), "1" ) << "text is never a number";
		EXPECT_EQ( Ports(nullptr), "1" );
	}

	TEST_F( NodeTests, SwitchTests ){
		Switch( R"({"rules":[{"t":"cont","v":"err"},{"t":"regex","v":"^P\\d+$","case":true},{"t":"true"},{"t":"false"},{"t":"null"},{"t":"nnull"},
			{"t":"empty"},{"t":"nempty"},{"t":"istype","v":"number"},{"t":"istype","v":"object"}]})", 10 );
		EXPECT_EQ( Ports("an error"), "0 5 7" );
		EXPECT_EQ( Ports("p12"), "1 5 7" );
		EXPECT_EQ( Ports("p12 "), "5 7" );
		EXPECT_EQ( Ports(true), "2 5" );
		EXPECT_EQ( Ports(false), "3 5" );
		EXPECT_EQ( Ports(nullptr), "4" );
		EXPECT_EQ( Ports(""), "5 6" );
		EXPECT_EQ( Ports(jarray{}), "5 6" );
		EXPECT_EQ( Ports(jarray{1, "err"}), "0 5 7" ) << "an array contains an element";
		EXPECT_EQ( Ports(7), "5 8" );
		EXPECT_EQ( Ports(jobject{{"a", 1}}), "5 7 9" );
	}

	TEST_F( NodeTests, SwitchOtherwiseAndFirstMatch ){
		Switch( R"({"rules":[{"t":"gt","v":5},{"t":"gt","v":7},{"t":"else"}]})", 3 );
		EXPECT_EQ( Ports(9), "0 1" );
		EXPECT_EQ( Ports(6), "0" );
		EXPECT_EQ( Ports(1), "2" );
		let twice = Host->Took->Of( "o1" ).at( 0 );
		EXPECT_EQ( twice.at("_parent"), Host->Took->Of("o0").at(0).at("_id") ) << "a clone down each output after the first";
		Stop();
		Host = ms<TestHost>();
		Switch( R"({"checkall":false,"rules":[{"t":"gt","v":5},{"t":"gt","v":7},{"t":"else"}]})", 3 );
		EXPECT_EQ( Ports(9), "0" ) << "the first rule that matches";
		EXPECT_EQ( Ports(1), "2" );
	}

	TEST_F( NodeTests, SwitchReadsAPropertyAgainstTheMessageAndTheContexts ){
		Context = { {"limit", 100} };
		Global->Set( "floor", 10 );
		Switch( R"({"property":"payload.speed","rules":[{"t":"gt","v":"limit","vt":"flow"},{"t":"lt","v":"floor","vt":"global"},{"t":"eq","v":"payload.target","vt":"msg"},{"t":"lt","v":"unset","vt":"flow"}]})", 4 );
		EXPECT_EQ( Ports(jobject{{"speed", 150}}), "0" );
		EXPECT_EQ( Ports(jobject{{"speed", 5}}), "1" );
		EXPECT_EQ( Ports(jobject{{"speed", 50}, {"target", 50}}), "2" );
		EXPECT_EQ( Ports(jobject{{"speed", 50}}), "" ) << "it matched no rule, and ended at the switch";
	}

	TEST_F( NodeTests, AnInt64DataChangeIsComparedAndScaledAsAnInteger ){
		Run( Nodes({
			Node( "count", "opc in", "{}", {{"sw"}} ),
			Node( "sw", "switch", R"({"rules":[{"t":"gt","v":9007199254740992},{"t":"else"}]})", {{"big"}, {"scale"}} ),
			Node( "scale", "range", R"({"minIn":0,"maxIn":1000,"minOut":0,"maxOut":1})", {{"out"}} ),
			Node( "big", "log" ), Node( "out", "log" )
		}) );
		Host->Pumps["count"]->Push( Value{jvalue{9007199254740993}, &UA_TYPES[UA_TYPES_INT64]} );//2^53+1, which a double reads as 2^53.
		Host->Pumps["count"]->Push( Value{jvalue{9007199254740992}, &UA_TYPES[UA_TYPES_INT64]} );
		Host->Pumps["count"]->Push( Value{jvalue{500}, &UA_TYPES[UA_TYPES_INT64]} );
		Sync();
		EXPECT_EQ( Took("big"), "9007199254740993" );
		ASSERT_EQ( Host->Took->Of("out").size(), 2u );
		EXPECT_DOUBLE_EQ( Last().at("payload").to_number<double>(), .5 );
		EXPECT_EQ( Last().at("status"), 0 );
	}

	TEST_F( NodeTests, Change ){
		One( "change", R"({"rules":[
			{"t":"set","p":"payload.unit","to":"rpm"},
			{"t":"set","p":"copy","to":"payload","tot":"msg"},
			{"t":"set","p":"last","pt":"flow","to":"payload.value","tot":"msg"},
			{"t":"set","p":"seen","pt":"global","to":{"by":"n"}},
			{"t":"move","p":"topic","to":"source"},
			{"t":"delete","p":"payload.raw"},
			{"t":"set","p":"limit","to":"limit","tot":"flow"},
			{"t":"move","p":"last","pt":"flow","to":"was"}
		]})" );
		Inject( jobject{{"value", 3}, {"raw", "x"}}, "pump1" );
		let took = Last();
		EXPECT_EQ( serialize(took.at("payload")), R"({"value":3,"unit":"rpm"})" );
		EXPECT_EQ( serialize(took.at("copy")), R"({"value":3,"raw":"x","unit":"rpm"})" ) << "a copy, as it stood at its rule";
		EXPECT_EQ( took.at("source"), "pump1" );
		EXPECT_FALSE( took.contains("topic") );
		EXPECT_FALSE( took.contains("limit") ) << "a value the context lacks takes the property away";
		EXPECT_EQ( took.at("was"), 3 );
		EXPECT_EQ( serialize(Global->Get("seen")), R"({"by":"n"})" );
		EXPECT_EQ( Changes, (vector<string>{"/last=3", "/last=-"}) ) << "the flow scope's changes, as the host is told them";
		EXPECT_EQ( Errors(), "" );
	}

	TEST_F( NodeTests, Filter ){
		One( "filter", "{}" );
		for( let payload : {1, 1, 2, 2, 1} )
			Inject( payload );
		EXPECT_EQ( Took(), "1 2 1" );
		Inject( 1.0 );
		EXPECT_EQ( Took(), "1 2 1" ) << "a number by its value";
		Inject( 1, "other" );
		Inject( 1, "other" );
		EXPECT_EQ( Took(), "1 2 1 1" ) << "per topic";
		Inject( jobject{{"a", 1}} );
		Inject( jobject{{"a", 1}} );
		Inject( jobject{{"a", 2}} );
		EXPECT_EQ( Host->Took->Of("out").size(), 6u );
		Stop();
		Host = ms<TestHost>();
		One( "filter", R"({"property":"payload.speed"})" );
		for( let& payload : {jobject{{"speed", 1}}, jobject{{"other", 1}}, jobject{{"speed", 1}}, jobject{{"speed", 2}}} )
			Inject( payload );
		EXPECT_EQ( Host->Took->Of("out").size(), 2u ) << "a message without the property ends at the filter";
		EXPECT_EQ( Errors(), "" );
	}

	TEST_F( NodeTests, FilterByABand ){
		One( "filter", R"({"band":5,"perTopic":false})" );
		for( let payload : {10, 12, 16, 14, 21, 22} )
			Inject( payload, payload%2 ? "odd" : "even" );
		EXPECT_EQ( Took(), "10 16 22" ) << "further than the band from the last one passed";
		Inject( "high" );
		EXPECT_EQ( Errors(), "'payload' is not a number to hold to a band." );
		Stop();
		Host = ms<TestHost>();
		One( "filter", R"({"band":10,"percent":true})" );
		for( let payload : {100, 105, 111, 120, 123} )
			Inject( payload );
		EXPECT_EQ( Took(), "100 111 123" );
	}

	TEST_F( NodeTests, Range ){
		One( "range", R"({"minIn":0,"maxIn":10,"minOut":0,"maxOut":100})" );
		Inject( 5 );
		EXPECT_DOUBLE_EQ( Last().at("payload").to_number<double>(), 50 );
		Inject( 15 );
		EXPECT_DOUBLE_EQ( Last().at("payload").to_number<double>(), 150 ) << "as it falls";
		Inject( "fast" );
		EXPECT_EQ( Errors(), "'payload' is not a number to scale." );
		Stop();
		Host = ms<TestHost>();
		One( "range", R"({"minIn":0,"maxIn":10,"minOut":100,"maxOut":0,"action":"clamp","round":true})" );
		Inject( 15 );
		EXPECT_EQ( Last().at("payload"), 0 );
		Inject( 2.56 );
		EXPECT_TRUE( Last().at("payload").is_int64() );
		EXPECT_EQ( Last().at("payload"), 74 );
		Stop();
		Host = ms<TestHost>();
		One( "range", R"({"property":"payload.angle","minIn":0,"maxIn":360,"minOut":0,"maxOut":1,"action":"wrap"})" );
		Inject( jobject{{"angle", 450}} );
		EXPECT_DOUBLE_EQ( Last().at("payload").at("angle").to_number<double>(), .25 );
		Inject( jobject{{"angle", -90}} );
		EXPECT_DOUBLE_EQ( Last().at("payload").at("angle").to_number<double>(), .75 );
	}

	TEST_F( NodeTests, Split ){
		One( "split", "{}" );
		Inject( jarray{"a", "b"}, "letters" );
		let took = Host->Took->Of( "out" );
		ASSERT_EQ( took.size(), 2u );
		let original = took[0].at( "_parent" );
		EXPECT_EQ( took[1].at("_parent"), original );
		EXPECT_NE( took[0].at("_id"), took[1].at("_id") );
		EXPECT_EQ( took[1].at("payload"), "b" );
		EXPECT_EQ( took[1].at("topic"), "letters" );
		EXPECT_EQ( serialize(took[1].at("parts")), Ƒ(R"({{"id":{},"index":1,"count":2,"type":"array"}})", serialize(original)) );
		Inject( jobject{{"x", 1}, {"y", 2}} );
		EXPECT_EQ( Took(), R"("a" "b" 1 2)" );
		EXPECT_EQ( Last().at("parts").at("key"), "y" );
		EXPECT_EQ( Last().at("parts").at("type"), "object" );
		Inject( 7 );
		EXPECT_EQ( Took(), R"("a" "b" 1 2 7)" ) << "any other payload passes";
		EXPECT_FALSE( Last().contains("parts") );
	}

	TEST_F( NodeTests, JoinPutsASplitBackTogether ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"parts"}} ), Node( "parts", "split", "{}", {{"whole"}} ),
			Node( "whole", "join", "{}", {{"out"}} ), Node( "out", "log" ),
			Node( "direct", "inject", "{}", {{"whole"}} ), Node( "caught", "catch", "{}", {{"errors"}} ), Node( "errors", "log" )
		}) );
		Inject( jarray{1, "two", jobject{{"three", 3}}}, "seq" );
		EXPECT_EQ( Took(), R"([1,"two",{"three":3}])" );
		EXPECT_EQ( Last().at("topic"), "seq" );
		EXPECT_TRUE( Last().contains("_parent") );
		Inject( jobject{{"x", 1}, {"y", jarray{2}}} );
		EXPECT_EQ( serialize(Last().at("payload")), R"({"x":1,"y":[2]})" );
		Inject( 5, {}, "direct" );
		EXPECT_EQ( Errors(), "the message has no `parts` to join by." );
	}

	TEST_F( NodeTests, JoinByCountAndByTopic ){
		One( "join", R"({"mode":"array","count":3})" );
		for( let payload : {1, 2, 3, 4} )
			Inject( payload );
		EXPECT_EQ( Took(), "[1,2,3]" ) << "the fourth starts the next";
		Stop();
		Host = ms<TestHost>();
		One( "join", R"({"mode":"object","count":2})" );
		Inject( 1, "a" );
		Inject( 2, "b" );
		Inject( 3, "a" );
		Inject( 4, "a" );
		Inject( 5, "b" );
		EXPECT_EQ( Took(), R"({"a":1,"b":2} {"a":4,"b":5})" ) << "a key again is its newest value";
		Inject( 6 );
		EXPECT_EQ( Errors(), "the message has no text to join it under." );
	}

	TEST_F( NodeTests, Template ){
		Context = { {"site", "North"} };
		Global->Set( "plant", jobject{{"name", "Acme"}} );
		One( "template", R"({"template":"{{topic}}: {{payload.speed}} rpm, {{{payload.note}}} {{ payload.note }} {{&topic}} at {{flow.site}} of {{global.plant.name}}{{! a comment }}{{absent}}."})" );
		Inject( jobject{{"speed", 1450}, {"note", "a<b"}}, "p&1" );
		EXPECT_EQ( Last().at("payload"), "p&amp;1: 1450 rpm, a<b a&lt;b p&1 at North of Acme." );
		Stop();
		Host = ms<TestHost>();
		One( "template", R"({"template":"{\"value\":{{{payload}}},\"from\":\"{{topic}}\"}","property":"body","output":"json"})" );
		Inject( jobject{{"a", 1}}, "t" );
		EXPECT_EQ( serialize(Last().at("body")), R"({"value":{"a":1},"from":"t"})" );
		EXPECT_EQ( serialize(Last().at("payload")), R"({"a":1})" );
		Inject( "not \" json", "t" );
		EXPECT_EQ( Host->Took->Of("errors").size(), 1u ) << Errors();
	}

	TEST_F( NodeTests, DebugTakesAndSendsNothing ){
		Run( Nodes({Node("in", "inject", "{}", {{"point"}}), Node("point", "debug", R"({"level":"Debug"})")}) );
		Inject( 1 );
		EXPECT_EQ( Counts("point").Received, 1u );
		EXPECT_TRUE( Counts("point").Sent.empty() );
		EXPECT_EQ( Trace->Lines().back(), "hop point []" );
	}
}
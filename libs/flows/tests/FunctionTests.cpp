//The Function node (docs/flows/spec.md *The Function node*):  Node-RED's API over the flow, the sandbox's limits, and the
//flow's clock under a body's timers and its `Date`.
#include <thread>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <jde/fwk/process/execution.h>
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	struct FunctionTests : FlowTest{
		//An inject into a Function node `f` of `code` into a log per output, `out` and then `out1` on, with a catch for what
		//the node fails and a complete node for what it finishes.
		α Body( sv code, jobject props={}, uint outputs=1, uint queue=1000 )->Flow&{
			props["code"] = code;
			if( outputs!=1 )
				props["outputs"] = outputs;
			jarray nodes{ Node("in", "inject", "{}", {{"f"}}), Node("caught", "catch", "{}", {{"errors"}}), Node("errors", "log"),
				Node("finished", "complete", R"({"scope":["f"]})", {{"done"}}), Node("done", "log") };
			jarray wires;
			for( uint i=0; i<outputs; ++i ){
				let out = i ? Ƒ( "out{}", i ) : string{ "out" };
				nodes.push_back( Node(out, "log") );
				wires.push_back( jarray{out} );
			}
			auto f = Node( "f", "function", serialize(props) );
			f["wires"] = move( wires );
			nodes.push_back( move(f) );
			Host = ms<TestHost>();
			return Run( jobject{{"nodes", move(nodes)}}, ETraceLevel::Hops, queue );
		}
		α Errors()->string{//each caught error's message.
			string y;
			for( let& message : Host->Took->Of("errors") )
				y += Ƒ( "{}{}", y.empty() ? "" : " | ", sv{message.at("error").at("message").as_string()} );
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
		α Id( sv node, uint i, sv key="_id" )->MessageId{
			let took = Host->Took->Of( node );
			return i<took.size() && took[i].contains( key ) ? took[i].at( key ).to_number<MessageId>() : 0;
		}
		//A message into a body that does not come back, and the wait for the body to say it runs, in the global store.
		α Spin()->void{
			Deployed->Inject( "in" );
			for( uint i=0; Global->Get("spinning").is_null() && i<5000; ++i )
				std::this_thread::sleep_for( 1ms );
			ASSERT_FALSE( Global->Get("spinning").is_null() ) << "the body never ran";
		}
		const string Timeout{ "It ran past the timeout of 50 ms." };
	};

	TEST( Function, ASettingsSizeReadsAsBytes ){
		EXPECT_EQ( ToBytes("16MiB"), 16u*1024*1024 );
		EXPECT_EQ( ToBytes("256KiB"), 256u*1024 );
		EXPECT_EQ( ToBytes("2GiB"), 2ull*1024*1024*1024 );
		EXPECT_EQ( ToBytes("4096"), 4096u );
		for( sv text : {"", "MiB", "16MB", "16 MiB", "1.5MiB", "-1KiB", "99999999999999999GiB"} )
			EXPECT_FALSE( ToBytes(text) ) << text;
	}

	TEST_F( FunctionTests, TheApiEndToEnd ){
		Env = { {"SITE", "plant 3"} };
		Context = { {"shift", "B"} };
		Body( R"(
			const n = ( context.get( "n" ) ?? 0 ) + 1;
			context.set( "n", n );
			flow.set( "last", msg.payload );
			global.set( "seen", { by: node.id, n } );
			context.flow.set( "gone", 1 );
			flow.set( "gone", undefined );
			node.status( { fill: "green", shape: "dot", text: `n=${n}` } );
			node.log( "a line" );
			node.warn( "a warning" );
			__block( "b1" );
			const copy = RED.util.cloneMessage( msg );
			copy.payload = { n, keys: context.keys(), flow: flow.keys(), shift: flow.get( "shift" ), seen: context.global.get( "seen" ).by, missing: flow.get( "nope" ) === undefined,
				site: env.get( "SITE" ), node: env.get( "NR_NODE_ID" ), name: node.name, flowName: env.get( "NR_FLOW_NAME" ), unset: env.get( "PATH" ) === undefined, same: copy !== msg && copy.topic === msg.topic };
			node.send( copy );
			return null;
		)" );
		Inject( 7, "rpm" );
		EXPECT_EQ( serialize(Last().at("payload")), R"({"n":1,"keys":["n"],"flow":["shift","last"],"shift":"B","seen":"f","missing":true,"site":"plant 3","node":"f","name":"","flowName":"test","unset":true,"same":true})" );
		EXPECT_EQ( Last().at("topic"), "rpm" );
		EXPECT_EQ( Deployed->Status("f"), (NodeStatus{"green", "dot", "n=1"}) );
		EXPECT_EQ( serialize(Global->Get("seen")), R"({"by":"f","n":1})" );
		Inject( 8 );
		EXPECT_EQ( Last().at("payload").at("n"), 2 );
		lg _{ ChangesMutex };
		EXPECT_EQ( Changes, (vector<string>{"f/n=1", "/last=7", "/gone=1", "/gone=-", "f/n=2", "/last=8", "/gone=1", "/gone=-"}) ) << "the host is told of each change, a key that went among them";
	}

	TEST_F( FunctionTests, ABodyReturnsAMessageAnArrayPerOutputOrNothing ){
		Body( R"(
			if( msg.payload === "none" )
				return;
			if( msg.payload === "one" )
				return { payload: "made here" };
			return [ msg, null, [ { payload: 1 }, { payload: 2 } ], { payload: "no such output" } ];
		)", {}, 3 );
		Inject( "none" );
		Inject( "one" );
		EXPECT_EQ( Took(), R"("made here")" );
		EXPECT_EQ( Id("out", 0), Id("done", 1, "_parent") ) << "what a body returns in place of its message is that message, going on";
		EXPECT_TRUE( Last().contains("_time") );
		Inject( "each" );
		EXPECT_EQ( Took(), R"("made here" "each")" );
		EXPECT_EQ( Took("out1"), "" );
		EXPECT_EQ( Took("out2"), "1 2" );
		EXPECT_EQ( Id("out2", 0, "_parent"), Id("out", 1) ) << "each other is a new message, the body's its parent";
		EXPECT_NE( Id("out2", 0), Id("out2", 1) );
		EXPECT_EQ( Host->Took->Of("done").size(), 3u ) << "each finished as its body returned";
		EXPECT_EQ( Errors(), "" );
		EXPECT_EQ( Counts("f").Sent, (vector<uint>{2, 0, 2}) );
	}

	TEST_F( FunctionTests, WhatIsNoMessageIsTheNodesError ){
		Body( R"(
			if( msg.payload === "number" )
				return 5;
			if( msg.payload === "cycle" ){
				msg.self = msg;
				return msg;
			}
			return [ [ "text" ] ];
		)" );
		Inject( "number" );
		Inject( "cycle" );
		Inject( "list" );
		EXPECT_EQ( Errors(), "TypeError: A message is an object. | TypeError: The value is nested past 64 levels, as one that holds itself is. | TypeError: A message is an object." );
		EXPECT_EQ( Took(), "" );
		EXPECT_EQ( Last("errors").at("payload"), "list" ) << "the catch takes the message as the node received it";
	}

	TEST_F( FunctionTests, ValuesCrossAsValues ){
		Body( R"(
			const p = msg.payload;
			return { payload: { at: new Date( 0 ), nan: 0 / 0, f(){}, u: undefined, list: [ undefined, 1.5, -0, 2 ** 53, 1e300 ], nested: { deep: [ { x: 1n } ] }, text: "é\u0000",
				types: [ typeof p.whole, typeof p.real, typeof p.big, typeof p.text, p.none === null, Array.isArray( p.list ), typeof msg._id ] } };
		)" );
		Inject( jobject{{"whole", 5}, {"real", 1.5}, {"big", 9007199254740993}, {"text", "t"}, {"none", nullptr}, {"list", jarray{1}}} );
		EXPECT_EQ( serialize(Last().at("payload")),
			R"({"at":"1970-01-01T00:00:00.000Z","nan":null,"list":[null,1.5E0,0,9.007199254740992E15,1E300],"nested":{"deep":[{"x":1}]},"text":"é\u0000","types":["number","number","bigint","string",true,true,"bigint"]})" );
	}

	TEST_F( FunctionTests, ABigIntCrossesAsAnInteger ){
		Run( Nodes({
			Node( "count", "opc in", "{}", {{"f"}} ),
			Node( "f", "function", R"({"code":"msg.kind = typeof msg.payload;\nmsg.payload = msg.kind === 'bigint' ? msg.payload + 1n : msg.payload * 2;\nreturn msg;"})", {{"sw"}} ),
			Node( "sw", "switch", R"({"rules":[{"t":"gt","v":9007199254740992},{"t":"else"}]})", {{"big"}, {"scale"}} ),
			Node( "scale", "range", R"({"minIn":0,"maxIn":1000,"minOut":0,"maxOut":1})", {{"out"}} ),
			Node( "big", "log" ), Node( "out", "log" )
		}) );
		Host->Pumps["count"]->Push( Value{jvalue{9007199254740993}, &UA_TYPES[UA_TYPES_INT64]} );//2^53+1, which a double reads as 2^53.
		Host->Pumps["count"]->Push( Value{jvalue{500}, &UA_TYPES[UA_TYPES_INT64]} );
		Sync();
		EXPECT_EQ( Took("big"), "9007199254740994" ) << "an Int64 past what a number holds is a BigInt in the body, and an integer again for the switch";
		EXPECT_EQ( Took("big", "kind"), R"("bigint")" );
		EXPECT_EQ( Took("out"), "1E0" ) << "one within is a number, as every other integer is";
		EXPECT_EQ( Took("out", "kind"), R"("number")" );

		Body( R"(
			const top = { signed: 2n ** 63n - 1n, least: -( 2n ** 63n ), unsigned: 2n ** 64n - 1n, small: 5n };
			if( msg.payload === "past" )
				top.past = 2n ** 64n;
			return { payload: top };
		)" );
		Inject( "fits" );
		EXPECT_EQ( serialize(Last().at("payload")), R"({"signed":9223372036854775807,"least":-9223372036854775808,"unsigned":18446744073709551615,"small":5})" );
		Inject( "past" );
		EXPECT_EQ( Errors(), "RangeError: A BigInt past 64 bits does not cross into a message." );
	}

	TEST_F( FunctionTests, NodeSendClonesUnlessToldNot ){
		Body( R"(
			node.send( msg );
			node.send( [ [ msg, msg ] ], false );
			return msg;
		)" );
		Inject( 1 );
		let received = Id( "done", 0, "_parent" );//what a complete node takes is a clone of the message its node finished.
		ASSERT_EQ( Host->Took->Of("out").size(), 4u );
		EXPECT_NE( Id("out", 0), received );
		EXPECT_EQ( Id("out", 0, "_parent"), received ) << "a send is a clone, the body's message its parent";
		EXPECT_EQ( Id("out", 1), received ) << "the first of a send told not to clone is the message itself";
		EXPECT_EQ( Id("out", 2, "_parent"), received );
		EXPECT_EQ( Id("out", 3, "_parent"), received ) << "a message goes on as itself once:  what the body returns after is a clone";
		EXPECT_EQ( std::set<MessageId>({Id("out", 0), Id("out", 1), Id("out", 2), Id("out", 3)}).size(), 4u );
	}

	TEST_F( FunctionTests, ABodyThatThrowsReachesTheCatch ){
		Body( "const limit = 10;\nif( msg.payload > limit )\n\tthrow new RangeError( `over ${limit}` );\nreturn msg;" );
		Inject( 5 );
		Inject( 50 );
		EXPECT_EQ( Took(), "5" );
		EXPECT_EQ( Errors(), "RangeError: over 10 (code:3:32)" );
		let caught = Last( "errors" );
		EXPECT_EQ( caught.at("payload"), 50 );
		EXPECT_EQ( caught.at("error").at("source").at("id"), "f" );
		EXPECT_EQ( caught.at("error").at("source").at("type"), "function" );
		EXPECT_EQ( Counts("f").Errors, 1u );
		EXPECT_EQ( Host->Took->Of("done").size(), 1u ) << "a message that failed did not complete";
	}

	TEST_F( FunctionTests, NodeErrorWithAMessageReachesTheCatchAndWithoutIsTheNodesOwn ){
		Body( R"(
			if( msg.payload === "caught" )
				node.error( new Error( "low" ), msg );
			else
				node.error( "only logged" );
			return null;
		)" );
		Inject( "caught" );
		EXPECT_EQ( Errors(), "Error: low" );
		EXPECT_EQ( Last("errors").at("payload"), "caught" );
		Inject( "logged" );
		EXPECT_EQ( Errors(), "Error: low" );
		EXPECT_EQ( Deployed->Status("f"), (NodeStatus{"red", "ring", "1 uncaught, the last:  only logged"}) );
		EXPECT_EQ( Counts("f").Errors, 2u );
	}

	TEST_F( FunctionTests, ARejectionNothingTakesIsTheNodesError ){
		Body( R"(
			Promise.reject( new Error( "lost" ) );
			Promise.reject( "handled" ).catch( () => {} );
			return msg;
		)" );
		Inject( 1 );
		EXPECT_EQ( Took(), "1" );
		EXPECT_TRUE( Has(Deployed->Status("f").Text, "1 uncaught, the last:  A promise was rejected and nothing took it:  Error: lost (code:2:24)") ) << Deployed->Status("f").Text;
	}

	TEST_F( FunctionTests, AnAsyncBodyReturnsWhenItsPromiseSettles ){
		Body( R"(
			if( msg.payload === "now" ){
				await Promise.resolve();
				msg.payload = await ( async () => "within the call" )();
				return msg;
			}
			if( msg.payload === "fails" ){
				await new Promise( wake => setTimeout( wake, 500 ) );
				throw new Error( "after the wait" );
			}
			await new Promise( wake => setTimeout( wake, 1000 ) );
			msg.payload = `${msg.payload} at ${new Date().toISOString()}`;
			return msg;
		)" );
		Inject( "now" );
		EXPECT_EQ( Took(), R"("within the call")" ) << "a body that awaits only its own promises is done within its call";
		Inject( "later" );
		Inject( "fails" );
		EXPECT_EQ( Took(), R"("within the call")" );
		EXPECT_EQ( Host->Took->Of("done").size(), 1u ) << "a message whose body awaits is the node's until it returns";
		Advance( 500ms );
		EXPECT_EQ( Errors(), "Error: after the wait (code:9:15)" );
		EXPECT_EQ( Last("errors").at("payload"), "fails" );
		Advance( 500ms );
		EXPECT_EQ( Took(), R"("within the call" "later at 2026-03-09T06:00:01.000Z")" ) << "it resumed at the clock's time";
		EXPECT_EQ( Id("out", 1), Id("done", 1, "_parent") ) << "and what it returned is the message it was called with";
		EXPECT_EQ( Host->Took->Of("done").size(), 2u );
	}

	TEST_F( FunctionTests, TheNodeHoldsNoMoreThanTheQueue ){
		Body( "await new Promise( () => {} );\nreturn msg;", {}, 1, 2 );
		for( uint i=0; i<3; ++i )
			Inject( i );
		EXPECT_EQ( Counts("f").Drops, 1u ) << "the oldest it held went, as a queue's does";
		EXPECT_EQ( Counts("f").Received, 3u );
	}

	TEST_F( FunctionTests, ABodyThatCallsNodeDoneFinishesItsOwnMessage ){
		Body( R"(
			setTimeout( () => {
				node.send( { payload: msg.payload * 2 } );
				node.done();
			}, 1000 );
		)" );
		Inject( 4 );
		EXPECT_EQ( Host->Took->Of("done").size(), 0u ) << "the body returned, and has yet to say it is done";
		Advance( 1s );
		EXPECT_EQ( Took(), "8" );
		EXPECT_EQ( Host->Took->Of("done").size(), 1u );
		EXPECT_EQ( Id("out", 0, "_parent"), Id("done", 0, "_parent") );
	}

	TEST_F( FunctionTests, TimersAndDateAreOnTheFlowsClock ){
		Body( R"(
			const at = new Date();
			setTimeout( ( a, b ) => node.send( { payload: [ a, b, new Date().toISOString(), Date.now() - at.getTime() ] } ), 1500, "x", 2 );
			return { payload: [ at.toISOString(), typeof performance, Date.now() === at.getTime(), new Date( 0 ).getTime(), new Date( 2026, 0, 2 ).getDate(), at instanceof Date,
				typeof Date(), Date.name, at.constructor === Date, Object.getPrototypeOf( Date ) === Function.prototype, Date.UTC( 1970, 0, 1 ), Date.parse( "1970-01-01T00:00:01Z" ) ] };
		)" );
		Inject( 1 );
		EXPECT_EQ( serialize(Last().at("payload")), R"(["2026-03-09T06:00:00.000Z","undefined",true,0,2,true,"string","Date",true,true,0,1000])" )
			<< "the manual clock's time, not the wall's, with no `performance` and no way back to the intrinsic";
		Advance( 1s );
		EXPECT_EQ( Host->Took->Of("out").size(), 1u );
		Advance( 1s );
		EXPECT_EQ( serialize(Last().at("payload")), R"(["x",2,"2026-03-09T06:00:01.500Z",1500])" );
	}

	TEST_F( FunctionTests, AnIntervalTicksUntilItIsCleared ){
		Body( R"(
			let n = 0;
			const id = setInterval( () => {
				node.send( { payload: ++n } );
				if( n === 3 )
					clearInterval( id );
			}, 0 );
			const never = setTimeout( () => node.send( { payload: "cleared" } ), 2 );
			clearTimeout( never );
			setTimeout( () => { throw new Error( "in a timer" ); }, 5 );
		)" );
		Inject( 1 );
		Advance( 1s );
		EXPECT_EQ( Took(), "1 2 3" ) << "a wait of nothing is a millisecond, so the clock moves on";
		EXPECT_TRUE( Has(Deployed->Status("f").Text, "1 uncaught, the last:  Error: in a timer (code:10:34)") ) << Deployed->Status("f").Text;
	}

	TEST_F( FunctionTests, OnStartAndOnStopRunAtADeployAndAStop ){
		let tabs = jobject{
			{"onStart", "context.set( 'started', ( context.get( 'started' ) ?? 0 ) + 1 );\nsetInterval( () => node.send( { payload: 'tick' } ), 1000 );"},
			{"onStop", "global.set( 'stopped', ( global.get( 'stopped' ) ?? 0 ) + 1 );\nawait null;\nglobal.set( 'awaited', true );"}
		};
		Body( "return msg;", tabs );
		Advance( 2s );
		EXPECT_EQ( Took(), R"("tick" "tick")" ) << "On Start ran before the flow's first message, and its timer is the node's";
		EXPECT_TRUE( Global->Get("stopped").is_null() );
		Stop();//as a redeploy stops the flow it replaces.
		EXPECT_EQ( Global->Get("stopped"), 1 ) << "On Stop ran as the flow stopped";
		EXPECT_EQ( Global->Get("awaited"), true ) << "with the jobs it left";
		NodeContext["f"] = { {"started", 1} };
		Body( "return msg;", tabs );
		{
			lg _{ ChangesMutex };
			EXPECT_EQ( Changes, (vector<string>{"f/started=1", "f/started=2"}) );
		}
		Stop();
		EXPECT_EQ( Global->Get("stopped"), 2 );

		auto graph = Graph::Read( Nodes({Node("f", "function", R"({"code":"return msg;","onStart":"\nthrow new Error( 'no licence' );"})")}) );
		ASSERT_TRUE( graph.has_value() );
		Flow flow{ move(*graph), {.Name="unstarted", .Clock=Time} };
		EXPECT_TRUE( flow.Start() );
		for( uint i=0; flow.Running() && i<1000; ++i )
			std::this_thread::sleep_for( 1ms );
		EXPECT_FALSE( flow.Running() ) << "an On Start that throws is the node's refusal";
		EXPECT_EQ( flow.Fault()->NodeId, "f" );
		EXPECT_EQ( flow.Fault()->Message, "On Start:  Error: no licence (onStart:2:11)" );
	}

	TEST_F( FunctionTests, ABodyThatRunsPastTheTimeoutIsItsNodesError ){
		Limits.Timeout = 50ms;
		Body( R"(
			if( msg.payload === "spin" )
				for(;;){}
			if( msg.payload === "catches" )
				for(;;){ try{ for(;;){} }catch( e ){} }
			if( msg.payload === "awaits" ){
				await null;
				for(;;){}
			}
			if( msg.payload === "generator" ){
				async function* each(){ for(;;){} }
				for(;;){ each().next(); }
			}
			return msg;
		)" );
		Inject( "spin" );
		EXPECT_EQ( Errors(), Timeout );
		Inject( "catches" );
		EXPECT_EQ( Errors(), Ƒ("{0} | {0}", Timeout) );
		Inject( "awaits" );
		EXPECT_EQ( Errors(), Ƒ("{0} | {0} | {0}", Timeout) ) << "a body past its first await is ended as one before it is, with its message";
		Inject( "generator" );
		EXPECT_EQ( Errors(), Ƒ("{0} | {0} | {0} | {0}", Timeout) ) << "what an async generator takes of the interrupt does not let its caller run on";
		EXPECT_EQ( Counts("f").Errors, 4u ) << "and the rejection it left is the interrupt, told once";
		Inject( "fine" );
		EXPECT_EQ( Took(), R"("fine")" ) << "the node runs on";
		EXPECT_EQ( Last("errors").at("payload"), "generator" );
	}

	TEST_F( FunctionTests, AJobPastTheTimeoutTakesTheMessagesThatWait ){
		Limits.Timeout = 50ms;
		Body( R"(
			if( msg.payload === "waits" ){
				await new Promise( wake => setTimeout( wake, 1000 ) );
				return msg;
			}
			Promise.resolve().then( () => { for(;;){} } );
			return msg;
		)" );
		Inject( "waits" );
		Inject( "returned" );
		EXPECT_EQ( Took(), R"("returned")" ) << "a body that returned has its answer, whatever a job of its does after";
		EXPECT_EQ( Errors(), Timeout ) << "whose message an interrupted job was is not known:  each the node still holds ends on it";
		EXPECT_EQ( Last("errors").at("payload"), "waits" );
		Advance( 1s );
		EXPECT_EQ( Took(), R"("returned")" );
	}

	TEST_F( FunctionTests, ABodyThatExhaustsTheMemoryIsItsNodesError ){
		Limits.Memory = 2*1024*1024;
		Body( R"(
			const kept = [];
			if( msg.payload === "arrays" )
				for(;;) kept.push( new Array( 1000 ).fill( 1 ) );
			if( msg.payload === "strings" )
				for( let text = "x";; ) kept.push( text += kept.length );
			if( msg.payload === "links" )
				for( let last = null;; ) last = { next: last };
			if( msg.payload === "null" )
				throw null;
			if( msg.payload === "large" )
				return { payload: new Array( 1e9 ) };
			return msg;
		)" );
		for( sv fill : {"arrays", "strings", "links", "null", "large"} )
			Inject( jvalue{fill} );
		EXPECT_EQ( Errors(), "InternalError: out of memory | InternalError: out of memory | InternalError: out of memory | InternalError: out of memory | RangeError: The value is larger than `function.memory`." )
			<< "whether quickjs-ng built the error or had no room to and threw null, and what a body builds outside its heap is bounded too";
		Inject( "fine" );
		EXPECT_EQ( Took(), R"("fine")" ) << "what the body held went with its call";
	}

	TEST_F( FunctionTests, ABodyThatFillsTheHeapAndCatchesEverythingIsEndedByTheTimeout ){
		Limits.Timeout = 50ms;
		Limits.Memory = 2*1024*1024;
		Body( R"(
			let kept = null;
			for(;;){
				try{
					for(;;) kept = { next: kept };
				}
				catch( e ){
					if( e !== null )
						throw e;
				}
			}
		)" );
		Inject( 1 );
		EXPECT_EQ( Errors(), Timeout ) << "the heap is full to where quickjs-ng has no room for an error and throws null, which the loop takes:  the interrupt's own is built in the reserve";
		Inject( 2 );
		EXPECT_EQ( Errors(), Ƒ("{0} | {0}", Timeout) );
	}

	TEST_F( FunctionTests, ABodyThatLoopsInAStackTraceHookIsEnded ){
		constexpr std::array<sv,2> hooks{
			"Error.prepareStackTrace = () => { for(;;){} };\nfor(;;){ try{ throw new Error( 'x' ); }catch( e ){} }",
			"Error.stackTraceLimit = { valueOf(){ for(;;){} } };\nfor(;;){ try{ null.x; }catch( e ){} }"
		};
		for( let hook : hooks ){
			Limits.Timeout = 50ms;
			Body( hook );
			Inject( 1 );
			EXPECT_EQ( Errors(), Timeout ) << hook;

			Limits.Timeout = 60s;
			Global = ms<GlobalStore>();
			Body( Ƒ("global.set( 'spinning', true );\n{}", hook) );
			Spin();
			let began = steady_clock::now();
			Stop();
			EXPECT_LT( steady_clock::now()-began, 5s ) << "a stop ends it:  " << hook;
			EXPECT_FALSE( Deployed->Running() );
		}
	}

	TEST_F( FunctionTests, ARunawayPromiseChainIsCutAtTheTimeout ){
		Limits.Timeout = 50ms;
		Body( R"(
			function again(){ Promise.resolve().then( again ); }
			again();
			again();//two chains:  an interrupt ends the one it falls in, and the other runs on.
			return msg;
		)" );
		Inject( 1 );
		EXPECT_EQ( Took(), "1" ) << "the body returned:  its message goes on";
		for( uint i=0; Counts("f").Errors<2 && i<5000; ++i )
			std::this_thread::sleep_for( 1ms );
		EXPECT_GE( Counts("f").Errors, 2u ) << "each entry the chain takes is cut at the timeout, and is the node's error";
		let began = steady_clock::now();
		Stop();
		EXPECT_LT( steady_clock::now()-began, 5s ) << "and the flow still stops";
	}

	TEST_F( FunctionTests, ABodyThatImportsHasNothingToLoad ){
		Body( R"(
			const fs = await import( "fs" );
			return msg;
		)" );
		Inject( 1 );
		EXPECT_EQ( Errors(), "ReferenceError: could not load module 'fs'" );
		EXPECT_EQ( Took(), "" );
		Body( R"(return { payload: [ typeof require, typeof process, typeof console, typeof std, typeof os, typeof globalThis.performance, typeof setTimeout, typeof __block ] };)" );
		Inject( 1 );
		EXPECT_EQ( serialize(Last().at("payload")), R"(["undefined","undefined","undefined","undefined","undefined","undefined","function","function"])" );
	}

	TEST_F( FunctionTests, ABodyThatRecursesPastTheStackIsItsNodesErrorOnAnyThread ){
		Body( R"(
			if( msg.payload === "deep" )
				( function down(){ down(); } )();
			return msg;
		)" );
		//Each call with the thread the strand last ran on held, so the strand takes another:  a task of the executor's that
		//waits, posted until that thread is the one that takes it.
		std::set<std::thread::id> threads;
		constexpr uint count{ 20 };
		for( uint i=0; i<count; ++i ){
			std::thread::id last;
			OnStrand( [&]{ last = std::this_thread::get_id(); } );
			sp<std::promise<void>> release;
			for( uint attempt=0; !release && attempt<10000; ++attempt ){
				std::promise<std::thread::id> held;
				auto gate = ms<std::promise<void>>();
				boost::asio::post( *Executor(), [&held, open=gate->get_future().share()]{
					held.set_value( std::this_thread::get_id() );
					open.wait();
				} );
				if( held.get_future().get()==last )
					release = gate;
				else
					gate->set_value();
			}
			Inject( "deep" );
			OnStrand( [&]{ threads.insert( std::this_thread::get_id() ); } );
			if( release )
				release->set_value();
			threads.insert( last );
		}
		EXPECT_GT( threads.size(), 1u ) << "the strand ran on more than one of the executor's threads";
		let errors = Host->Took->Of( "errors" );
		ASSERT_EQ( errors.size(), count ) << "the limit is below where each call was entered, on whichever thread";
		for( let& error : errors )
			EXPECT_TRUE( Has(string{error.at("error").at("message").as_string()}, "RangeError: Maximum call stack size exceeded") );
		Inject( "fine" );
		EXPECT_EQ( Took(), R"("fine")" );
	}

	//reviews-issue/450-flows-function-node.md #2:  a graph's Function nodes are checked together, in one sandbox under its
	//limits, as the flow makes them;  #13:  a read of the graph's shape makes none;  #1:  a sandbox that runs out partway
	//through making a node leaks nothing, which quickjs-ng's Debug build asserts as the runtime goes.
	TEST( Function, AGraphsFunctionNodesAreCheckedTogetherUnderItsLimits ){
		let functions = []( uint count, sv code="return msg;" ){
			jarray nodes;
			for( uint i=0; i<count; ++i )
				nodes.push_back( Node(Ƒ("f{}", i), "function", serialize(jobject{{"code", code}})) );
			return jvalue{ jobject{{"nodes", move(nodes)}} };
		};
		const FunctionLimits small{ .Memory=256*1024 };
		EXPECT_TRUE( Graph::Read(functions(1), false, small).has_value() ) << "one fits";
		let five = Graph::Read( functions(5), false, small );
		ASSERT_FALSE( five.has_value() ) << "five do not, together";
		EXPECT_NE( five.error().NodeId, "f0" ) << five.error().Message;
		EXPECT_TRUE( Graph::Read(functions(5)).has_value() ) << "the default limits hold five";

		EXPECT_FALSE( Graph::Read(functions(1, "return msg +;")).has_value() );
		EXPECT_TRUE( Graph::Read(functions(1, "return msg +;"), false, nullopt).has_value() ) << "a read of the graph's shape makes no node";

		for( uint memory=64*1024; memory<=512*1024; memory+=512 )
			std::ignore = Graph::Read( functions(4), false, FunctionLimits{.Memory=memory} );//a leak aborts the suite.
	}

	//#5:  a timeout while one message's answer is read, a getter that spins, is that message's:  the others are read in
	//the next entry, where they can still call.
	TEST_F( FunctionTests, ATimeoutReadingOneAnswerLeavesTheOthersToTheNextEntry ){
		Limits.Timeout = 50ms;
		Body( R"(
			globalThis.gate ??= new Promise( wake => globalThis.open = wake );
			if( msg.payload === "go" ){
				open();
				return null;
			}
			await gate;
			if( msg.payload === "spins" )
				return { get payload(){ for(;;){} } };
			return { payload: new Date( 0 ) };
		)" );
		Inject( "spins" );
		Inject( "dated" );
		Inject( "go" );
		for( uint i=0; Took().empty() && i<5000; ++i )
			Sync();
		EXPECT_EQ( Errors(), Timeout );
		EXPECT_EQ( Last("errors").at("payload"), "spins" );
		EXPECT_EQ( Took(), R"("1970-01-01T00:00:00.000Z")" ) << "its Date's toJSON was called in the next entry";
		EXPECT_EQ( Host->Took->Of("done").size(), 2u ) << "and the rest finished";
	}

	//#6:  a stop while On Start runs is no refusal:  the flow stops with no fault.
	TEST_F( FunctionTests, AStopUnderOnStartIsNoRefusal ){
		auto graph = Graph::Read( Nodes({Node("f", "function", R"({"code":"return msg;","onStart":"global.set( 'spinning', true );\nfor(;;){}"})")}) );
		ASSERT_TRUE( graph.has_value() );
		Flow flow{ move(*graph), {.Name="stopped", .Clock=Time, .Global=Global, .Function={.Timeout=60s}} };
		ASSERT_TRUE( flow.Start() );
		for( uint i=0; Global->Get("spinning").is_null() && i<5000; ++i )
			std::this_thread::sleep_for( 1ms );
		ASSERT_FALSE( Global->Get("spinning").is_null() ) << "On Start never ran";
		std::promise<void> stopped;
		flow.Stop( [&]{ stopped.set_value(); } );
		ASSERT_EQ( stopped.get_future().wait_for(5s), std::future_status::ready ) << "a stop ends it";
		EXPECT_FALSE( flow.Fault() ) << flow.Fault()->Message;
		EXPECT_NE( flow.Status("f").Fill, "red" ) << flow.Status( "f" ).Text;
	}

	//#7:  a timeout that falls between two jobs is the node whose job ran last, which the time went to, not the one whose
	//job is next.  heavy's jobs take the time in a few calls of long built-ins, between which the interrupt is not polled,
	//each queueing the next first so an interrupt in one leaves the chain;  light's take none, and each follows one of heavy's.
	TEST_F( FunctionTests, ATimeoutBetweenJobsIsTheNodeWhoseJobRanLast ){
		Limits.Timeout = 50ms;
		Host = ms<TestHost>();
		Run( Nodes({
			Node( "in", "inject", "{}", {{"heavy", "light"}} ),
			Node( "heavy", "function", R"({"code":"function again(){ Promise.resolve().then( again ); 'x'.repeat( 1e6 ).indexOf( 'y' ); }\nagain();\nreturn null;"})" ),
			Node( "light", "function", R"({"code":"function again(){ Promise.resolve().then( again ); }\nagain();\nreturn null;"})" )
		}) );
		Deployed->Inject( "in" );
		for( uint i=0; Counts("heavy").Errors<3 && i<5000; ++i )
			std::this_thread::sleep_for( 1ms );
		EXPECT_GE( Counts("heavy").Errors, 3u );
		EXPECT_EQ( Counts("light").Errors, 0u ) << "light's jobs take no time:  each deadline passed in one of heavy's";
	}

	//#8:  an On Start past the timeout after its first await is the node's refusal, as one before it is:  quickjs-ng ends
	//an async function on the interrupt without settling its promise.
	TEST_F( FunctionTests, AnOnStartPastTheTimeoutAfterAnAwaitIsTheNodesRefusal ){
		for( sv onStart : {"for(;;){}", "await null;\nfor(;;){}"} ){
			auto graph = Graph::Read( Nodes({Node("f", "function", serialize(jobject{{"code", "return msg;"}, {"onStart", onStart}}))}) );
			ASSERT_TRUE( graph.has_value() );
			Flow flow{ move(*graph), {.Name="unstarted", .Clock=Time, .Function={.Timeout=50ms}} };
			EXPECT_TRUE( flow.Start() );
			for( uint i=0; flow.Running() && i<1000; ++i )
				std::this_thread::sleep_for( 1ms );
			EXPECT_FALSE( flow.Running() ) << onStart;
			ASSERT_TRUE( flow.Fault() ) << onStart;
			EXPECT_EQ( flow.Fault()->Message, "On Start:  It ran past the timeout of 50 ms." ) << onStart;
		}
	}

	//#9:  rejections nothing takes are the node's errors, ten an entry, and the rest counted in one.
	TEST_F( FunctionTests, RejectionsNothingTakesAreToldTenAnEntry ){
		Body( "for( let i=0; i<1000; ++i )\n\tPromise.reject( new Error(`n${i}`) );\nreturn msg;" );
		Inject( 1 );
		EXPECT_EQ( Took(), "1" );
		EXPECT_EQ( Counts("f").Errors, 11u ) << "ten told, and one for the rest";
		EXPECT_TRUE( Has(Deployed->Status("f").Text, "990 more promises were rejected and nothing took them.") ) << Deployed->Status("f").Text;
	}

	//#10:  `node.done` before an await finishes the message then, the body's promise still pending, and what the body
	//returns after goes as a message of its own.
	TEST_F( FunctionTests, NodeDoneBeforeAnAwaitFinishesTheMessage ){
		Body( R"(
			node.send( msg );
			node.done();
			await new Promise( wake => setTimeout( wake, 60000 ) );
			return { payload: "later" };
		)" );
		Inject( 1 );
		EXPECT_EQ( Took("done"), "1" ) << "finished as `done` was called, not a minute on";
		Advance( 60s );
		EXPECT_EQ( Took(), R"(1 "later")" );
		EXPECT_EQ( Took("done"), "1" ) << "and once";
		EXPECT_EQ( Id("out", 1, "_parent"), Id("done", 0, "_parent") ) << "what it returned is a message of its own, the finished one its parent";
	}
}
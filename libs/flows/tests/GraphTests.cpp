//The graph's reading and each refusal of its validation (docs/flows/spec.md *Flows*), the fault at the node the editor marks.
#include "helpers.h"

namespace Jde::Opc::Flows::Tests{
	//The fault of a graph, as `node:  message`;  empty for one that reads.
	Ξ Refused( const jvalue& graph )->string{
		let read = Graph::Read( graph );
		return read ? string{} : Ƒ( "{}:  {}", read.error().NodeId, read.error().Message );
	}
	//A graph of one node of `type` with `props`, fed by an inject and feeding a log.
	Ξ RefusedProps( sv type, sv props )->string{
		return Refused( Nodes({Node("in", "inject", "{}", {{"n"}}), Node("n", type, props, {{"out"}}), Node("out", "log")}) );
	}
	#define EXPECT_FAULT( refused, node, part ){ let fault = refused; EXPECT_TRUE( fault.starts_with(string{node}+":") && Has(fault, part) ) << fault; }

	TEST( Graph, Reads ){
		let read = Graph::Read( Nodes({
			Node( "in", "inject", R"({"message":{"topic":"t"}})", {{"sw", "out"}} ),
			Node( "sw", "switch", R"({"rules":[{"t":"gt","v":5},{"t":"else"}]})", {{"out"}, {}} ),
			Node( "note", "comment" ),
			Node( "out", "log" )
		}) );
		ASSERT_TRUE( read.has_value() ) << read.error().Message;
		ASSERT_EQ( read->Nodes.size(), 4u );
		EXPECT_EQ( read->Nodes[0].Type, ENodeType::Inject );
		EXPECT_EQ( read->Nodes[0].Wires, (vector<vector<uint32>>{{1, 3}}) );
		EXPECT_EQ( read->Nodes[1].Wires, (vector<vector<uint32>>{{3}, {}}) );
		EXPECT_EQ( read->IndexOf("out"), 3u );
		EXPECT_FALSE( read->IndexOf("nowhere") );
		EXPECT_EQ( Refused(Nodes({})), "" ) << "an empty graph is a graph";
	}

	TEST( Graph, TypeNames ){
		for( uint8 i=0; i<=(uint8)ENodeType::HttpRequest; ++i )
			EXPECT_EQ( ToNodeType(ToString((ENodeType)i)), (ENodeType)i ) << ToString( (ENodeType)i );
		EXPECT_EQ( ToString(ENodeType::LinkOut), "link out" );
		EXPECT_FALSE( ToNodeType("rbe") );
		EXPECT_TRUE( IsHosted(ENodeType::OpcIn) && IsHosted(ENodeType::HttpRequest) && !IsHosted(ENodeType::Inject) && !IsHosted(ENodeType::Switch) );
		EXPECT_TRUE( HasInput(ENodeType::Log) && !HasInput(ENodeType::Catch) && !HasInput(ENodeType::LinkIn) );
	}

	TEST( Graph, RefusesTheShape ){
		EXPECT_FAULT( Refused(jarray{}), "", "`nodes` list" );
		EXPECT_FAULT( Refused(jobject{{"nodes", 5}}), "", "`nodes` list" );
		EXPECT_FAULT( Refused(parse(R"({"nodes":[{"type":"log"}]})")), "", "Node 1 of the graph has no `id`" );
		EXPECT_FAULT( Refused(parse(R"({"nodes":[{"id":"a","type":"log"},7]})")), "", "Node 2 of the graph has no `id`" );
		EXPECT_FAULT( Refused(Nodes({Node("a", "log"), Node("a", "log")})), "a", "Two nodes have this id" );
		EXPECT_FAULT( Refused(Nodes({Node("a", "log"), Node("b", "rbe")})), "b", "\"rbe\" is not a node type" );
		EXPECT_FAULT( Refused(parse(R"({"nodes":[{"id":"a"}]})")), "a", "is missing" );
		EXPECT_FAULT( Refused(parse(R"({"nodes":[{"id":"a","type":"log","props":[]}]})")), "a", "`props` must be an object" );
		EXPECT_FAULT( Refused(parse(R"({"nodes":[{"id":"a","type":"inject","wires":["b"]}]})")), "a", "`wires` must be a list per output port" );
	}

	TEST( Graph, RefusesWires ){
		EXPECT_FAULT( Refused(Nodes({Node("in", "inject", "{}", {{"gone"}})})), "in", "'gone', which is not a node of the graph" );
		EXPECT_FAULT( Refused(Nodes({Node("in", "inject", "{}", {{"other"}}), Node("other", "inject")})), "in", "a 'inject' node takes no input" );
		EXPECT_FAULT( Refused(Nodes({Node("in", "inject", "{}", {{"c"}}), Node("c", "catch")})), "in", "a 'catch' node takes no input" );
		EXPECT_FAULT( Refused(Nodes({Node("in", "inject", "{}", {{"out"}, {"out"}}), Node("out", "log")})), "in", "Output 2 is wired, and a 'inject' node has 1" );
		EXPECT_FAULT( Refused(Nodes({Node("in", "inject", "{}", {{"out"}}), Node("out", "log", "{}", {{"in"}})})), "out", "Output 1 is wired, and a 'log' node has 0" );
		EXPECT_FAULT( Refused(Nodes({
			Node( "in", "inject", "{}", {{"sw"}} ),
			Node( "sw", "switch", R"({"rules":[{"t":"true"}]})", {{"out"}, {"out"}} ),
			Node( "out", "log" )
		})), "sw", "Output 2 is wired, and a 'switch' node has 1" );
		EXPECT_EQ( Refused(Nodes({Node("in", "inject", "{}", {{"out"}}), Node("out", "log", "{}", {{}})})), "" ) << "a port with no wire is no fault";
	}

	TEST( Graph, RefusesProps ){
		EXPECT_FAULT( RefusedProps("switch", "{}"), "n", "`rules` must be a list of rules" );
		EXPECT_FAULT( RefusedProps("switch", R"({"rules":[{"t":"near","v":1}]})"), "n", "`t` must be one of eq, neq" );
		EXPECT_FAULT( RefusedProps("switch", R"({"rules":[{"t":"eq"}]})"), "n", "the rule has no `v`" );
		EXPECT_FAULT( RefusedProps("switch", R"({"rules":[{"t":"btwn","v":1}]})"), "n", "the rule has no `v2`" );
		EXPECT_FAULT( RefusedProps("switch", R"({"rules":[{"t":"regex","v":"("}]})"), "n", "is not a regular expression" );
		EXPECT_FAULT( RefusedProps("switch", R"({"rules":[{"t":"istype","v":"buffer"}]})"), "n", "`v` must be one of string, number" );
		EXPECT_FAULT( RefusedProps("switch", R"({"rules":[{"t":"eq","v":"limit","vt":"env"}]})"), "n", "`vt` must be one of msg, flow, global" );
		EXPECT_FAULT( RefusedProps("switch", R"({"property":"payload..x","rules":[{"t":"true"}]})"), "n", "is not a property's path" );
		EXPECT_FAULT( RefusedProps("change", R"({"rules":[{"t":"set","p":"_id","to":1}]})"), "n", "`_id` is the engine's" );
		EXPECT_FAULT( RefusedProps("change", R"({"rules":[{"t":"set","p":"payload"}]})"), "n", "the rule has no `to`" );
		EXPECT_FAULT( RefusedProps("change", R"({"rules":[{"t":"move","p":"payload","to":7}]})"), "n", "`to` must be text" );
		EXPECT_FAULT( RefusedProps("filter", R"({"percent":true})"), "n", "`percent` is of a `band`" );
		EXPECT_FAULT( RefusedProps("filter", R"({"band":"wide"})"), "n", "`band` must be a number" );
		EXPECT_FAULT( RefusedProps("range", R"({"minIn":0,"maxIn":0,"minOut":0,"maxOut":1})"), "n", "the input range is empty" );
		EXPECT_FAULT( RefusedProps("range", R"({"minIn":0,"maxIn":10,"minOut":0})"), "n", "`maxOut` must be a number" );
		EXPECT_FAULT( RefusedProps("delay", "{}"), "n", "`duration` must be an ISO 8601 duration" );
		EXPECT_FAULT( RefusedProps("delay", R"({"duration":"five seconds"})"), "n", "`duration` must be an ISO 8601 duration" );
		EXPECT_FAULT( RefusedProps("delay", R"({"mode":"rate","per":"PT1S","count":0})"), "n", "`count` must be a whole number above zero" );
		EXPECT_FAULT( RefusedProps("trigger", R"({"wait":"PT30S"})"), "n", "a trigger sends a `first`, a `second` or both" );
		EXPECT_FAULT( RefusedProps("trigger", R"({"first":1})"), "n", "`wait` must be an ISO 8601 duration" );
		EXPECT_FAULT( RefusedProps("window", R"({"duration":"PT1M","count":5})"), "n", "by `duration` or by `count`, one of the two" );
		EXPECT_FAULT( RefusedProps("window", "{}"), "n", "by `duration` or by `count`, one of the two" );
		EXPECT_FAULT( RefusedProps("join", R"({"mode":"array"})"), "n", "needs a `count` or a `timeout`" );
		EXPECT_FAULT( RefusedProps("template", "{}"), "n", "`template` must be text" );
		EXPECT_FAULT( RefusedProps("template", R"({"template":"{{#rows}}x{{/rows}}"})"), "n", "no sections, partials or delimiters" );
		EXPECT_FAULT( RefusedProps("template", R"({"template":"{{payload"})"), "n", "a tag that does not close" );
		EXPECT_FAULT( RefusedProps("function", R"({"code":"return msg;"})"), "n", "not built yet" );
		EXPECT_FAULT( Refused(Nodes({Node("in", "inject", R"({"message":5})")})), "in", "`message` must be an object" );
		EXPECT_FAULT( Refused(Nodes({Node("d", "debug", R"({"level":"Loud"})")})), "d", "`level` must be one of Trace, Debug" );
		EXPECT_EQ( RefusedProps("opc out", R"({"anything":[1,2]})"), "" ) << "a hosted node's props are its host's";
	}

	TEST( Graph, RefusesScopesAndLinks ){
		EXPECT_FAULT( Refused(Nodes({Node("c", "catch", R"({"scope":["gone"]})")})), "c", "`scope` names \"gone\"" );
		EXPECT_FAULT( Refused(Nodes({Node("c", "catch", R"({"uncaught":"yes"})")})), "c", "`uncaught` must be true or false" );
		EXPECT_FAULT( Refused(Nodes({Node("s", "status", R"({"scope":"all"})")})), "s", "`scope` must be a list of node ids" );
		EXPECT_FAULT( Refused(Nodes({Node("c", "complete")})), "c", "`scope` must name the nodes" );
		EXPECT_EQ( Refused(Nodes({Node("out", "log"), Node("c", "complete", R"({"scope":["out"]})")})), "" );
		EXPECT_FAULT( Refused(Nodes({Node("in", "inject", "{}", {{"lo"}}), Node("lo", "link out", R"({"name":"a"})")})), "lo", "No `link in` of the flow is named 'a'" );
		EXPECT_FAULT( Refused(Nodes({Node("lo", "link out")})), "lo", "`name` must be text" );
		EXPECT_FAULT( Refused(Nodes({Node("a", "link in", R"({"name":"a"})"), Node("b", "link in", R"({"name":"a"})")})), "a", "Two `link in` nodes are named 'a'" );
		EXPECT_EQ( Refused(Nodes({Node("li", "link in", R"({"name":"a"})")})), "" ) << "an input nothing feeds is no fault";
	}

	TEST( Graph, AnswersTheFirstFault ){
		EXPECT_FAULT( Refused(Nodes({
			Node( "in", "inject", "{}", {{"d"}} ),
			Node( "d", "delay" ),
			Node( "w", "window" )
		})), "d", "`duration`" );
	}
}
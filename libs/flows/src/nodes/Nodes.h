#pragma once
#include <jde/flows/Flow.h>

//The stock nodes (docs/flows/spec.md *Nodes*), each Node-RED's where it has one.  A constructor reads the node's props to
//their types and throws for one that is not, which is how a graph is checked:  by making its nodes, a Function node in
//the sandbox its graph's share.
namespace Jde::Opc::Flows{
	//The library's node for a definition.  Null for a type with nothing of its own to run:  an inject, a catch, a status, a
	//complete and a comment are the engine's.  Throws for a prop that is not of its type, and for a type the library does
	//not make here:  a Function node is MakeFunction's.
	α MakeNode( const NodeDef& def, const Graph& graph )ε->up<INode>;
	α Outputs( const NodeDef& def )ι->uint;//the node's output ports:  a switch's, one per rule.
	//A catch's, a status's or a complete's `scope`, the ids of the nodes it takes from, as indexes:  empty for every node.
	α Scope( const NodeDef& def, const Graph& graph )ε->flat_set<uint32>;

	α MakeSwitch( const jobject& props )ε->up<INode>;
	α MakeChange( const jobject& props )ε->up<INode>;
	α MakeFilter( const jobject& props )ε->up<INode>;
	α MakeRange( const jobject& props )ε->up<INode>;
	α MakeDelay( const jobject& props )ε->up<INode>;
	α MakeTrigger( const jobject& props )ε->up<INode>;
	α MakeWindow( const jobject& props )ε->up<INode>;
	α MakeSplit( const jobject& props )ε->up<INode>;
	α MakeJoin( const jobject& props )ε->up<INode>;
	α MakeTemplate( const jobject& props )ε->up<INode>;
	α MakeDebug( const jobject& props )ε->up<INode>;
	α MakeLink( const jobject& props )ε->up<INode>;//either end of a link:  what it receives it sends on.

	//The Function node (src/js):  a flow's sandbox, the one runtime its Function nodes share, and a node in it.  Making the
	//node compiles its body, its On Start and its On Stop, and throws for one that does not parse.
	namespace Js{ struct Runtime; }
	α MakeSandbox( const FunctionLimits& limits, string flow, jobject env )ε->sp<Js::Runtime>;
	α Halt( Js::Runtime& sandbox )ι->void;//any thread:  what runs in it ends, as past its timeout, and but an On Stop nothing more starts.
	α MakeFunction( const NodeDef& def, sp<Js::Runtime> sandbox )ε->up<INode>;
	α FunctionOutputs( const jobject& props )ι->std::expected<uint,string>;//its `outputs`, 1 by default, or why it is not one.
}
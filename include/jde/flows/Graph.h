#pragma once
#include <expected>

//A flow's graph (docs/flows/spec.md *Flows*):  `{ nodes: [ { id, type, name, x, y, props, wires } ] }`, Node-RED's shape,
//read and held to the rules a deploy refuses by.
namespace Jde::Opc::Flows{
	//Every type of the spec's *Sources*, *Nodes* and *Sinks*.
	enum class ENodeType : uint8{
		OpcIn, GroupIn, Timer, Calendar, Connection, Inject, LinkIn,
		Switch, Change, Filter, Range, Delay, Trigger, Window, Split, Join, Template, Function, Debug, Catch, Status, Complete, LinkOut, Comment,
		OpcOut, Variable, Log, HttpRequest
	};
	α ToString( ENodeType type )ι->sv;//the graph's `type`:  "opc in", "link out".
	α ToNodeType( sv name )ι->optional<ENodeType>;
	α IsHosted( ENodeType type )ι->bool;//a source or a sink the host makes (IHost);  the library carries the rest.
	α IsSource( ENodeType type )ι->bool;//of *Sources*.
	α HasInput( ENodeType type )ι->bool;//whether a wire may end at it.

	struct NodeDef final{
		α Label()Ι->str{ return Name.empty() ? Id : Name; }
		string Id;
		ENodeType Type{};
		string Name;
		jobject Props;
		vector<vector<uint32>> Wires;//per output port, the indexes in Graph::Nodes of the nodes it feeds.
	};
	//What a deploy refuses by, with the node the editor marks:  none when the fault is the graph's own.
	struct Fault final{
		string NodeId;
		string Message;
	};
	struct Graph final{
		//The first fault, in node order:  an id missing or used twice, a type that is not one, a wire to no node of the graph,
		//to one that takes no input or from a port the node lacks, a property of a library node that is not of its type, a
		//catch, status or complete naming no node, and a `link out` whose name no `link in` of the graph holds.  A hosted
		//node's props are its host's to check.  `hostedLinks`:  such a `link out` is no fault but its host's, which knows the
		//other flows:  IHost::Sink makes it.
		Ω Read( const jvalue& json, bool hostedLinks=false )ι->std::expected<Graph,Fault>;
		α IndexOf( sv id )Ι->optional<uint32>;
		vector<NodeDef> Nodes;
	};
}
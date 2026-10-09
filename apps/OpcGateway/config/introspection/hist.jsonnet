//hist( opc, nodes, start, end, modified, returnBounds, limit, continuation ){ continuation values{…} nodes{…} } (ql/HistQLAwait.cpp):
//a server's own history, read for the caller (libs/historian/docs/spec.md, *Pass-through*).  Config-only types - no view
//behind them, so __type(name:...) is answered from here alone.  A group's read (Phase 5) answers the same shape.
//NodeId is search.jsonnet's.
//histInsert|histReplace|histUpdate( opc, values:[{ node source server status value }] ), histDelete( opc, nodes, start, end )
//and histDeleteAtTime( opc, nodes, times ){ values{…} nodes{…} } (ql/HistEditQLAwait.cpp):  the server's own history,
//edited for the caller (*Pass-through*);  mutations by their whole names.
local String = { kind: "SCALAR", name: "String" };
local UInt = { kind: "SCALAR", name: "UInt" };
local Boolean = { kind: "SCALAR", name: "Boolean" };
local DateTime = { kind: "SCALAR", name: "DateTime" };
local Json = { kind: "SCALAR", name: "Json" }; //a value as /opc carries it:  a number, string, boolean, array or object.
local NonNull(t) = { kind: "NON_NULL", name: null, ofType: t };
local List(t) = { kind: "LIST", name: null, ofType: t };
local histEdit = {
	fields: [
		{ name: "values", type: List({ kind: "OBJECT", name: "HistEditValue" }) }, //each value, or time, as the edit names it.
		{ name: "nodes", type: List({ kind: "OBJECT", name: "HistNode" }) } //each node's entry status, as the server answered it.
	]
};
local hist = {
	fields: [
		{ name: "continuation", type: String }, //for the next page; null on the last.
		{ name: "values", type: List({ kind: "OBJECT", name: "HistValue" }) }, //in source-time order, later first in a reverse read.
		{ name: "nodes", type: List({ kind: "OBJECT", name: "HistNode" }) } //each node's status, as the server answered it.
	]
};
{
	Hist: hist, //the type's canonical name.
	hist: hist, //the query's name.
	HistValue: {
		fields: [
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } },
			{ name: "source", type: DateTime },
			{ name: "server", type: DateTime },
			{ name: "status", type: NonNull(UInt) },
			{ name: "value", type: Json },
			{ name: "bound", type: NonNull(Boolean) }, //one of the bounds returnBounds asked for.
			{ name: "heartbeat", type: NonNull(Boolean) }, //a group's stored repeat of its last value; never a server's.
			{ name: "modification", type: { kind: "OBJECT", name: "HistModification" } } //with modified: true.
		]
	},
	HistModification: {
		fields: [
			{ name: "time", type: NonNull(DateTime) },
			{ name: "type", type: NonNull(String) }, //Insert, Replace, Update or Delete.
			{ name: "user", type: NonNull(String) }
		]
	},
	HistEdit: histEdit,
	histInsert: histEdit,
	histReplace: histEdit,
	histUpdate: histEdit,
	histDelete: histEdit, //values is empty:  a range delete answers per node.
	histDeleteAtTime: histEdit,
	HistEditValue: {
		fields: [
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } },
			{ name: "source", type: DateTime }, //the value's source time, or the time deleted.
			{ name: "status", type: NonNull(UInt) } //the server's operation result, or its node's entry status.
		]
	},
	HistNode: {
		fields: [
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } },
			{ name: "status", type: NonNull(UInt) }
		]
	}
}

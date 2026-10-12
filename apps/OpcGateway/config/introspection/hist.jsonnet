//history( opc, nodes, start, end, modified, returnBounds, limit, continuation ){ continuation values{…} nodes{…} } (ql/HistQLAwait.cpp),
//history( opc, nodes, times, limit, continuation ) and history( opc, nodes, start, end, interval, aggregate, limit, continuation )
//(ql/HistComputedQLAwait.cpp):  a server's own history, raw, at times or aggregated, read for the caller (libs/historian/docs/spec.md,
//*Pass-through*).  Config-only types - no view behind them, so __type(name:...) is answered from here alone.  With `group` in place
//of `opc`, a gateway group's own history, in the same three modes and the same shape (ql/HistGroupReadQLAwait.cpp, *Reads*).
//NodeId is search.jsonnet's.
//createHistory|updateHistory|upsertHistory( opc, values:[{ node source server status value }] ) and purgeHistory( opc, nodes,
//start, end ) or purgeHistory( opc, nodes, times ){ values{…} nodes{…} } (ql/HistEditQLAwait.cpp):  the server's own
//history, edited for the caller (*Pass-through*);  mutations by their whole names.  With `group`, a gateway group's history
//(ql/HistGroupEditQLAwait.cpp, *Edits*).
//resolveHistory( group ){ nodes{ templateNode path node status } } (ql/HistGroupQLAwait.cpp):  retries a group's unresolved
//template members (*Templates*), answering each member of the template, resolved before, now, or not.
local String = { kind: "SCALAR", name: "String" };
local UInt = { kind: "SCALAR", name: "UInt" };
local Boolean = { kind: "SCALAR", name: "Boolean" };
local DateTime = { kind: "SCALAR", name: "DateTime" };
local Json = { kind: "SCALAR", name: "Json" }; //a value as /opc carries it:  a number, string, boolean, array or object.
local NonNull(t) = { kind: "NON_NULL", name: null, ofType: t };
local List(t) = { kind: "LIST", name: null, ofType: t };
local historyEdit = {
	fields: [
		{ name: "values", type: List({ kind: "OBJECT", name: "HistoryEditValue" }) }, //each value, or time, as the edit names it.
		{ name: "nodes", type: List({ kind: "OBJECT", name: "HistoryNode" }) } //each node's entry status, as the server, or the group's historian, answered it.
	]
};
local resolveHistory = {
	fields: [
		{ name: "nodes", type: List({ kind: "OBJECT", name: "HistoryResolvedNode" }) }
	]
};
local history = {
	fields: [
		{ name: "continuation", type: String }, //for the next page; null on the last.
		{ name: "values", type: List({ kind: "OBJECT", name: "HistoryValue" }) }, //in source-time order, later first in a reverse read.
		{ name: "nodes", type: List({ kind: "OBJECT", name: "HistoryNode" }) } //each node's status, as the server answered it:  for a group's, Bad_NodeIdUnknown for a node it doesn't hold.
	]
};
{
	History: history, //the type's canonical name.
	history: history, //the query's name.
	HistoryValue: {
		fields: [
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } },
			{ name: "source", type: DateTime },
			{ name: "server", type: DateTime },
			{ name: "status", type: NonNull(UInt) },
			{ name: "value", type: Json },
			{ name: "bound", type: NonNull(Boolean) }, //one of the bounds returnBounds asked for.
			{ name: "heartbeat", type: NonNull(Boolean) }, //a group's stored repeat of its last value; never a server's.
			{ name: "modification", type: { kind: "OBJECT", name: "HistoryModification" } } //with modified: true.
		]
	},
	HistoryModification: {
		fields: [
			{ name: "time", type: NonNull(DateTime) },
			{ name: "type", type: NonNull(String) }, //Insert, Replace, Update or Delete.
			{ name: "user", type: NonNull(String) }
		]
	},
	HistoryEdit: historyEdit,
	createHistory: historyEdit,
	updateHistory: historyEdit,
	upsertHistory: historyEdit,
	purgeHistory: historyEdit, //values is empty for a range:  it answers per node.
	HistoryEditValue: {
		fields: [
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } },
			{ name: "source", type: DateTime }, //the value's source time, or the time deleted.
			{ name: "status", type: NonNull(UInt) } //the server's, or the historian's, operation result, or its node's entry status.
		]
	},
	HistoryNode: {
		fields: [
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } },
			{ name: "status", type: NonNull(UInt) }
		]
	},
	ResolveHistory: resolveHistory,
	resolveHistory: resolveHistory,
	HistoryResolvedNode: {
		fields: [
			{ name: "templateNode", type: NonNull(UInt) }, //the hist_template_nodes row.
			{ name: "path", type: NonNull(String) }, //its browse path, as the row holds it.
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } }, //with its namespace URI (nsu); null where the path didn't resolve.
			{ name: "status", type: NonNull(UInt) } //Good once the member has a hist_group_nodes row, else why TranslateBrowsePaths refused it.
		]
	}
}
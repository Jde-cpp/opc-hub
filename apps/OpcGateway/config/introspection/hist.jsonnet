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
//resolveHistory( group ){ nodes{ templateNode path node status holder{ id name } } } (ql/HistGroupQLAwait.cpp):  retries a group's
//unresolved template members (*Templates*), answering each member of the template, resolved before, now, or not - and for one
//whose node another group holds, which.
//historyGroups( opc, nodes ){ groups{ id name nodes } } (ql/HistGroupInfoQLAwait.cpp):  the groups on the connection that hold
//any of the nodes, each with those it holds, among the groups the caller may read - where the gateway keeps each node's history.
//historyCapabilities( group ){ aggregates update delete }:  the aggregates a group computes, and whether the caller may edit
//its history, Update and Delete granted on an enforced resource (*Authorization*).
//historyThresholds( group ){ update nodes{ id node templateNode path enumeration band thresholds{ name value source } } }:  the
//group's member list - each member's thresholds as the group resolves them, with where each comes from, whether its deviation
//band is in force, and whether the caller may override them (updateHistGroupNode).
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
local historyGroups = {
	fields: [
		{ name: "groups", type: List({ kind: "OBJECT", name: "HistoryGroup" }) }
	]
};
local historyCapabilities = {
	fields: [
		{ name: "aggregates", type: List(NonNull(String)) }, //the names an aggregate read with `group` takes, in the library's order.
		{ name: "update", type: NonNull(Boolean) }, //createHistory, updateHistory and upsertHistory would pass the caller.
		{ name: "delete", type: NonNull(Boolean) } //purgeHistory would.
	]
};
local historyThresholds = {
	fields: [
		{ name: "update", type: NonNull(Boolean) }, //updateHistGroupNode would pass the caller:  the table's Update right, which an unenforced resource passes.
		{ name: "nodes", type: List({ kind: "OBJECT", name: "HistoryThresholdNode" }) }
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
	HistoryGroups: historyGroups,
	historyGroups: historyGroups,
	HistoryGroup: {
		fields: [
			{ name: "id", type: NonNull(UInt) }, //the hist_groups row.
			{ name: "name", type: NonNull(String) },
			{ name: "nodes", type: List({ kind: "OBJECT", name: "NodeId" }) } //those of the nodes asked that the group holds, spelled as a read spells them back.
		]
	},
	HistoryCapabilities: historyCapabilities,
	historyCapabilities: historyCapabilities,
	HistoryThresholds: historyThresholds,
	historyThresholds: historyThresholds,
	HistoryThresholdNode: {
		fields: [
			{ name: "id", type: NonNull(UInt) }, //the hist_group_nodes row, which updateHistGroupNode names.
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } }, //with its namespace URI (nsu), and the server's index (ns) once the collector has it.
			{ name: "templateNode", type: UInt }, //the hist_template_nodes row it was resolved from;  null for a node added by hand.
			{ name: "path", type: String }, //that member's browse path, as the row holds it;  null for a node added by hand.
			{ name: "enumeration", type: Boolean }, //whether the server said its DataType is an enumeration, which takes no deviation band;  null until it has.
			{ name: "band", type: String }, //whether its deviation band is in force:  none (no deviation), applied, enumeration, noRange (a percent format the node has no usable range for, so every change is stored) or waiting (until the server has described the node);  null for a member the group doesn't hold.
			{ name: "thresholds", type: List({ kind: "OBJECT", name: "HistoryThreshold" }) } //the five, in the columns' order.
		]
	},
	HistoryThreshold: {
		fields: [
			{ name: "name", type: NonNull(String) }, //the column's name:  exceptionDeviation, exceptionDeviationFormat, minTimeInterval, maxTimeInterval or stepped.
			{ name: "value", type: Json }, //the effective value:  a number, the intervals in milliseconds, stepped a boolean;  null for a deviation none sets.
			{ name: "source", type: NonNull(String) } //where it comes from:  node (the row's own), template (its member's), group or default.
		]
	},
	HistoryResolvedNode: {
		fields: [
			{ name: "templateNode", type: NonNull(UInt) }, //the hist_template_nodes row.
			{ name: "path", type: NonNull(String) }, //its browse path, as the row holds it.
			{ name: "node", type: { kind: "OBJECT", name: "NodeId" } }, //with its namespace URI (nsu); null where the path didn't resolve.
			{ name: "status", type: NonNull(UInt) }, //Good once the member has a hist_group_nodes row, else why TranslateBrowsePaths refused it, or Bad_NodeIdExists for a node a group holds already.
			{ name: "holder", type: { kind: "OBJECT", name: "HistoryGroupRef" } } //with Bad_NodeIdExists:  the group that holds the node - another on the connection, or this one as another member or an ad hoc node.  Null otherwise.
		]
	},
	HistoryGroupRef: {
		fields: [
			{ name: "id", type: NonNull(UInt) }, //the hist_groups row.
			{ name: "name", type: String } //null for a group the caller may not read, which historyGroups wouldn't list.
		]
	}
}
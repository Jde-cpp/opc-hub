//The flows' system mutations (docs/flows/spec.md *Flows*, *Sources*, *Rights*), registered by their whole names, so the schema
//introspects neither itself.  Config-only types - no view behind them, so __type(name:...) is answered from here alone.
//deployFlow( flow, graph, comment ){ flow version deployed userName running dropped fault{ node message } } (ql/FlowMAwait.cpp,
//flows/FlowHost.cpp):  the graph, as json or its text, read and admitted for the caller, stored as the flow's next version and,
//for an enabled flow, started in place of the one running.  Update on the flow.  A graph that does not read, or that its
//deployer's rights do not admit, is refused with `Node '<id>':  <why>`, and nothing is written.
//injectFlow( flow, node, message ){ flow node }:  the inject node's message sent, or `message` in its place.  Update on the flow.
//setFlowContext( flow, node, name, value ){ flow node name value }:  a context value set, in a running flow's store and as its
//flow_contexts row, or taken away by a null `value`.  No `flow` is the gateway's scope, no `node` the flow's.  Update on the
//flow, the table's for the gateway's scope.
//The trace's reads (flows/FlowQLAwait.cpp, spec *History*), each Read on the flow, paged at /gateway/flows/readLimit:
//flowTrace( flow, start, end, node, msg, kind, run, limit, continuation ){ continuation records{…} }:  the records in a range.
//flowMessage( flow, id, start, end, limit ){ more records{…} }:  a message's lineage.
//flowCounts( flow, start, end, by ){ counts{…} }:  the minute figures summed into buckets.
local types = import 'scalars.libsonnet';
local String = types.String, UInt = types.UInt, Boolean = types.Boolean, DateTime = types.DateTime, Json = types.Json, Object = types.Object, NonNull = types.NonNull, List = types.List;
local flowTraceRecord = {
	fields: [
		{ name: "time", type: NonNull(DateTime) }, //the flow's clock:  a hop's, when the node received the message.
		{ name: "msgId", type: Json }, //a Long;  null for a status change.
		{ name: "parentId", type: Json }, //the message this one was cloned or made from;  null at a source.
		{ name: "runId", type: NonNull(UInt) }, //0 live, a replay's otherwise.
		{ name: "node", type: NonNull(String) }, //the node's id in the graph.
		{ name: "kind", type: NonNull(String) }, //hop, birth, clone, error, status or drop.
		{ name: "ports", type: List(UInt) }, //the output ports it was sent on;  always a list, which the loader cannot say with NON_NULL around LIST.
		{ name: "json", type: Json }, //the message, where the record carries it:  a birth's, and a hop's as sent where the node changed it;  its text where `cut`.
		{ name: "cut", type: NonNull(Boolean) }, //the json was cut at valueLimit.
		{ name: "error", type: String },
		{ name: "status", type: Object("FlowNodeStatus") },
		{ name: "blocks", type: List(String) } //the ids of the blocks a block-built Function body passed, in order.
	]
};
local flowTrace = {
	fields: [
		{ name: "continuation", type: String }, //for the next page;  null on the last.
		{ name: "records", type: List(Object("FlowTraceRecord")) }
	]
};
local flowMessage = {
	fields: [
		{ name: "more", type: NonNull(Boolean) }, //the limit cut the lineage short.
		{ name: "records", type: List(Object("FlowTraceRecord")) }
	]
};
local flowCounts = {
	fields: [
		{ name: "counts", type: List(Object("FlowCount")) }
	]
};
local deployFlow = {
	fields: [
		{ name: "flow", type: NonNull(UInt) }, //the flows row.
		{ name: "version", type: NonNull(UInt) }, //the version written, the flow's current one.
		{ name: "deployed", type: NonNull(DateTime) },
		{ name: "userName", type: NonNull(String) }, //the deployer's name, as the version holds it.
		{ name: "running", type: NonNull(Boolean) }, //false for a disabled flow, and for one that did not start.
		{ name: "dropped", type: NonNull(UInt) }, //the messages the run before it held or had queued, dropped as it stopped.
		{ name: "fault", type: Object("FlowFault") } //why an enabled flow did not start;  null otherwise.
	]
};
local injectFlow = {
	fields: [
		{ name: "flow", type: NonNull(UInt) },
		{ name: "node", type: NonNull(String) }
	]
};
local setFlowContext = {
	fields: [
		{ name: "flow", type: UInt }, //null for the gateway's scope.
		{ name: "node", type: String }, //null for the flow's scope, and the gateway's.
		{ name: "name", type: NonNull(String) }, //the key.
		{ name: "value", type: String } //as the row holds it, json text;  null for a key taken away.
	]
};
{
	DeployFlow: deployFlow,
	deployFlow: deployFlow,
	InjectFlow: injectFlow,
	injectFlow: injectFlow,
	SetFlowContext: setFlowContext,
	setFlowContext: setFlowContext,
	FlowTrace: flowTrace,
	flowTrace: flowTrace,
	FlowMessage: flowMessage,
	flowMessage: flowMessage,
	FlowCounts: flowCounts,
	flowCounts: flowCounts,
	FlowTraceRecord: flowTraceRecord,
	FlowNodeStatus: {
		fields: [
			{ name: "fill", type: NonNull(String) },
			{ name: "shape", type: NonNull(String) },
			{ name: "text", type: NonNull(String) }
		]
	},
	FlowCount: {
		fields: [
			{ name: "time", type: NonNull(DateTime) }, //the bucket's start.
			{ name: "node", type: NonNull(String) },
			{ name: "received", type: NonNull(UInt) },
			{ name: "sent", type: List(UInt) }, //per output port.
			{ name: "drops", type: NonNull(UInt) },
			{ name: "errors", type: NonNull(UInt) }
		]
	},
	FlowFault: {
		fields: [
			{ name: "node", type: String }, //the node's id in the graph;  null for a fault of the flow's own.
			{ name: "message", type: NonNull(String) }
		]
	}
}
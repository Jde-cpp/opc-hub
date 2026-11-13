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
local types = import 'scalars.libsonnet';
local String = types.String, UInt = types.UInt, Boolean = types.Boolean, DateTime = types.DateTime, Object = types.Object, NonNull = types.NonNull;
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
	FlowFault: {
		fields: [
			{ name: "node", type: String }, //the node's id in the graph;  null for a fault of the flow's own.
			{ name: "message", type: NonNull(String) }
		]
	}
}
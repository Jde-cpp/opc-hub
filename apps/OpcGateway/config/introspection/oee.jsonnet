//The OEE system mutations (docs/oee/spec.md *The engine*, *Authorization*), registered by their whole names, so the schema
//introspects none of them itself.  Config-only types - no view behind them, so __type(name:...) is answered from here alone.
//recomputeOee( machine, start, end ){ machine start end } (ql/OeeMAwait.cpp):  the machine's rows over start to end made
//again from its group's history (oee/OeeMachineHost.cpp), for a caller with Update on oee_machines and Read on the group;
//answers the span queued, as the engine ran it, once it is queued, not done.
local types = import 'scalars.libsonnet';
local UInt = types.UInt, DateTime = types.DateTime, NonNull = types.NonNull;
local recomputeOee = {
	fields: [
		{ name: "machine", type: NonNull(UInt) }, //the oee_machines row.
		{ name: "start", type: NonNull(DateTime) }, //the span's, as asked.
		{ name: "end", type: NonNull(DateTime) }
	]
};
{
	RecomputeOee: recomputeOee,
	recomputeOee: recomputeOee
}
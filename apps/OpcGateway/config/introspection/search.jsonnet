//search( opc?, text, limit?, refresh? ){ connection{slug name} id path name browse{ns name} nodeClass depth } (ql/SearchQLAwait.cpp):
//node names matched against the gateway's per-connection NodeIndex.  Config-only types - no view behind them, so __type(name:...)
//is answered from here alone.
local types = import 'scalars.libsonnet';
local String = types.String, UInt = types.UInt, NonNull = types.NonNull;
local search = {
	fields: [
		{ name: "connection", type: { kind: "OBJECT", name: "SearchConnection" } },
		{ name: "id", type: { kind: "OBJECT", name: "NodeId" } },
		{ name: "path", type: NonNull(String) },
		{ name: "name", type: NonNull(String) },
		{ name: "browse", type: { kind: "OBJECT", name: "BrowseName" } },
		{ name: "nodeClass", type: NonNull(UInt) },
		{ name: "depth", type: NonNull(UInt) }
	]
};
{
	Search: search, //the type's canonical name.
	search: search, //the query's name.
	SearchConnection: {
		fields: [
			{ name: "slug", type: NonNull(String) },
			{ name: "name", type: NonNull(String) }
		]
	},
	BrowseName: {
		fields: [
			{ name: "ns", type: NonNull(UInt) },
			{ name: "name", type: NonNull(String) }
		]
	},
	NodeId: {
		fields: [
			{ name: "ns", type: UInt },
			{ name: "id", type: String }
		]
	}
}
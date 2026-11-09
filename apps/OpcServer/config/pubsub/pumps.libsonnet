// OPC UA PubSub (Part 14) contract for the pump process values - the ONE place the DataSet is described.
// Imported by the OpcServer's emulator overlays (config/Opc.Server.Emulator[.Hub].jsonnet -> /opcServer/pubsub, the
// DataSetReader - NOT the stock Opc.Server.jsonnet: a reader is an unauthenticated write into the address space, so it
// is opt-in) and by the PLC emulator (emulator/config/Opc.PlcEmulator.jsonnet -> /emulator/pubsub, the DataSetWriter),
// so the DataSetMetaData both sides build from `fields` can't drift: a mismatch makes the reader silently drop every
// message.
//
// Only process values are published.  `status` is the run command the UI writes on the OpcServer; the emulator
// subscribes to it over its client session and never publishes it (publishing would clobber the UI's write).
//
// `path` is a browse path under Objects (i=85) - `<ns>~<name>` segments, `~` being BrowseName's separator; the `pumps`
// alias is resolved to the runtime index of urn:jde:pumps by each process (never a hard-coded index - it moves with the
// nodeset load order).  Field order is the wire order.
//
// url: the UADP multicast group the standard examples use.  Multicast needs a route (a box with a default route has
// one; a bare container may not) - the unicast form "opc.udp://127.0.0.1:<port>/" is the fallback, and what the test
// configs use.  Plaintext: this open62541 build has no SKS.
{
	transportProfile: "http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp",
	url: "opc.udp://224.0.0.22:4840/",
	networkInterface: "",
	publisherId: 2234,
	writerGroupId: 100,
	dataSetWriterId: 1,
	publishingInterval: "PT1S",
	dataSet: {
		name: "pumps",
		namespace: "urn:jde:pumps",
		fields: [
			{ name: "pump1.motorRpm", type: "Double", path: "pumps~pump1/pumps~motorRpm" },
			{ name: "pump2.motorRpm", type: "Double", path: "pumps~pump2/pumps~motorRpm" },
			{ name: "pump3.motorRpm", type: "Double", path: "pumps~pump3/pumps~motorRpm" },
			{ name: "pump4.motorRpm", type: "Double", path: "pumps~pump4/pumps~motorRpm" },
			{ name: "pumpManual.motorRpm", type: "Double", path: "pumps~pumpManual/pumps~motorRpm" },
			//OEE's counts and stop code (docs/oee/spec.md *Emulator*), after the speeds so those keep their place in the wire order.
			{ name: "pump1.totalCount", type: "UInt32", path: "pumps~pump1/pumps~totalCount" },
			{ name: "pump1.rejectCount", type: "UInt32", path: "pumps~pump1/pumps~rejectCount" },
			{ name: "pump1.reasonCode", type: "Int32", path: "pumps~pump1/pumps~reasonCode" },
			{ name: "pump2.totalCount", type: "UInt32", path: "pumps~pump2/pumps~totalCount" },
			{ name: "pump2.rejectCount", type: "UInt32", path: "pumps~pump2/pumps~rejectCount" },
			{ name: "pump2.reasonCode", type: "Int32", path: "pumps~pump2/pumps~reasonCode" },
			{ name: "pump3.totalCount", type: "UInt32", path: "pumps~pump3/pumps~totalCount" },
			{ name: "pump3.rejectCount", type: "UInt32", path: "pumps~pump3/pumps~rejectCount" },
			{ name: "pump3.reasonCode", type: "Int32", path: "pumps~pump3/pumps~reasonCode" },
			{ name: "pump4.totalCount", type: "UInt32", path: "pumps~pump4/pumps~totalCount" },
			{ name: "pump4.rejectCount", type: "UInt32", path: "pumps~pump4/pumps~rejectCount" },
			{ name: "pump4.reasonCode", type: "Int32", path: "pumps~pump4/pumps~reasonCode" },
			{ name: "pumpManual.totalCount", type: "UInt32", path: "pumps~pumpManual/pumps~totalCount" },
			{ name: "pumpManual.rejectCount", type: "UInt32", path: "pumps~pumpManual/pumps~rejectCount" },
			{ name: "pumpManual.reasonCode", type: "Int32", path: "pumps~pumpManual/pumps~reasonCode" }
		]
	}
}

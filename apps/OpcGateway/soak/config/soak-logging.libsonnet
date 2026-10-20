//The soak overlays' log tag maps.  Each replaces its process's production map, whose Trace/Debug tags are unbounded over 24h (the
//text sink never rotates):  everything at Information, and open62541's categories at Warning in the processes that parse them
//(Opc::UALogParser).  The AppServer parses none, and an unknown tag is a startup warning, so it takes `plain`.
{
	plain:: { default: "Information" },
	ua:: self.plain + {
		uaNet: "Warning",
		uaSecure: "Warning",
		uaSession: "Warning",
		uaServer: "Warning",
		uaClient: "Warning",
		uaUser: "Warning",
		uaSecurity: "Warning",
		uaEvent: "Warning",
		uaPubSub: "Warning",
		uaDiscovery: "Warning"
	}
}
// Soak overlay: production gateway config with sync=true baked in and log tags flattened - the production map has
// http/socket read+write at Debug and app/browse/ql at Trace, unbounded over 24h (the text sink never rotates).
// gateway.pingInterval/ttl stay at production values - the idle-teardown path is under test.
// Launch with -include=../../config/args/sqlite (relative to this file) and -arg path=<run>/db/gateway.db.
local base = (import '../../config/Opc.Gateway.jsonnet')(sync=true);
base + {
	gateway+: {
		//The soak's servers are whatever is to hand - the Jde OpcServer overlay one run, Kepware the next - and a rung
		//refused BadCertificateUntrusted because a server's certificate was never copied under /gateway/trustedCertDirs
		//is a wasted 24h, not a finding.  Off here only;  the production config keeps it on.
		verifyServerCertificate: false
	},
	ql+: {
		//introspection paths resolve relative to the settings file's directory - point back at the production config dir.
		introspection: [ '../../config/' + p for p in base.ql.introspection ]
	},
	logging+: {
		spd+: {
			tags: {
				default: "Information",
				monitoring: "Information",
				processingLoop: "Information",
				uaNet: "Warning",
				uaClient: "Warning",
				uaSecure: "Warning",
				uaSession: "Warning",
				uaServer: "Warning",
				uaUser: "Warning",
				uaSecurity: "Warning",
				uaPubSub: "Warning",
				uaDiscovery: "Warning"
			}
		}
	}
}

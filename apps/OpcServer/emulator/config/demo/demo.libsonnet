// A demo PLC (docs/demo/spec.md *The PLCs*):  Opc.PlcEmulator.Install.jsonnet for PLC `n`, 1 to 5, with its machine's figures -
// `machine` is { cycle, jitter, reject, stops, slow }, the spec's PLC table, which each plc<N>.jsonnet passes so the soak tunes a
// machine in its own file.  Launch:  `-settings=config/demo/plc<N>.jsonnet -include=../args/install` (`../args/dev` on a dev box,
// the nodeset from the source tree);  the include is relative to the settings file's dir.
//
// Each PLC has two certificates in two product dirs, since productName chooses the dir and the CN the file (CryptoSettings.cpp):
// the login's under $(ProgramData)/Jde-Cpp/PlcEmulator.plc<N> (http.ssl, CN plc<N>, the hub user it enrolls as), which the hub's
// access.trustedCertDirs lists, and the channel's under PlcEmulator.plc<N>.opc (emulator.ssl, CN plc<N>.opc, SAN the applicationUri,
// the PLC server's pair too through plc.ssl's default), which the gateway's trustedCertDirs lists - no dir in both.  The log goes
// beside the login certificates:  five processes in one dir would share one Opc.PlcEmulator.log.
local args = import 'args.libsonnet';
function( n, machine ) (import '../Opc.PlcEmulator.Install.jsonnet') + {
	local name = "plc"+n,
	local uri = "urn:jde:demo:"+name,
	instanceName: name, //the PLC server's ApplicationName names it, "Jde-Cpp PlcEmulator [plc1]".
	http+:{ ssl+:{ productName: "PlcEmulator."+name, certificate+:{ commonName: name } } },
	emulator+:{
		transport: "server",
		applicationUri: uri,
		zone::: "America/New_York", //the plant's, the seed's calendar's (spec *The plant's rows*): the `at` stops' and slow periods' clock.
		namespace::: "urn:jde:demo", //the device path's, demo.NodeSet2.xml's.
		ssl+:{ productName: "PlcEmulator."+name+".opc", certificate+:{ commonName: name+".opc", subjectAltName: "URI:"+uri } },
		plc+:{
			port: 4841+n, //4842 to 4846, on plc.address's loopback default:  only this box's gateway dials them.
			nodeset: args.nodesetsDir+"/demo.NodeSet2.xml",
			//every visitor on a PLC's node browser holds a session of its own beside the collector's:  above open62541's 100.
			limits+::: { maxSessions: 200, maxSecureChannels: 250 }
		},
		tightening:: null, //one Open Protocol station per box at most: five processes can't share 4545, and the demo has none.
		//The one machine, demo.NodeSet2.xml's Machine (spec *Production*):  `status` is on but for the stops, the count runs at the
		//machine's cycle with its reject share on rejectCount, `speed` lags towards the count's pace in units a minute, and the parser
		//adds reasonCode for the stops.  No seed:  each run draws its random stops afresh and logs the seed it drew.
		devices: [ {
			path: "Machine",
			stops: machine.stops,
			slow: machine.slow,
			tags: [
				{ name: "status", mode: "on" },
				{ name: "totalCount", mode: "production", cycle: machine.cycle, jitter: machine.jitter, reject: machine.reject },
				{ name: "speed", mode: "follow", follows: "totalCount", tau: "PT5S" }
			]
		} ]
	},
	logging+:{ spd+:{ sinks+:{ file+:{ path: args.companyDir+"/PlcEmulator."+name } } } }
}
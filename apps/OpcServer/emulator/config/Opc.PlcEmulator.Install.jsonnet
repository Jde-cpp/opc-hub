// Opc.PlcEmulator.Hub.jsonnet as installed:  the base of a demo PLC's overlay in config/demo, which replaces the nodeset and its
// namespace.  Alone (`-settings=<this file> -include=args/dev`) it serves the pumps on a dev box, the nodeset read from the source
// tree;  no installed box has pumps.NodeSet2.xml, the demo tarball laying demo.NodeSet2.xml alone (demo plan Phase 8).  The base
// config is dev-shaped wherever it reaches outside the product dir - two ext vars, `logsDir` and `buildTarget`, which Settings
// binds only under -tests, the pumps contract imported from the source tree and a $(JDE_DIR) nodeset - and each of those is
// replaced from the install args here;  the hub overlay gives the login TLS anchor.  jsonnet is lazy, so the base's `std.extVar`
// reads and its pubsub import are never forced once shadowed (as Opc.Server.Install.jsonnet relies on), and the installed tree
// need not carry the contract.
//
// An installed emulator is a PLC for the hub's gateway, -transport=server:  the OpcServer's unit ships disabled and has no
// PubSub reader, so neither other transport could run.  The hub must trust the login certificate's dir in return
// (apps/OpcHub/config/args, access.trustedCertDirs) and the gateway the channel's (gateway.trustedCertDirs) - the demo's
// args/demo and Opc.Hub.Demo.jsonnet list the five PLCs' (demo plan Phase 8).
local args = import 'args.libsonnet';
(import 'Opc.PlcEmulator.Hub.jsonnet') + {
	instanceName: args.instanceName,
	//the hub, by ip: `localhost` resolves to ::1 and then 127.0.0.1, each refused while the hub is not up yet (the OpcServer's
	//install config says why); the hub's certificate names IP:127.0.0.1, so the login's host check passes.
	server+:{ host: "127.0.0.1" },
	emulator+:{
		transport: "server",
		opcSchema: "opc.install", ///opcServer/resource = "install" in Opc.Server.Install.jsonnet; read by -grant alone, which the server transport never needs.
		pubsub:: null, //the contract is the source tree's and the pubsub transport's; none installed.
		namespace::: "urn:jde:pumps", //the pumps nodeset's, which the base reads from the contract hidden above.
		trustedCertDirs: [ args.certsDir("OpcServer") ],
		plc+:{
			nodeset: args.nodesetsDir+"/pumps.NodeSet2.xml",
			//the hub's own dir alone, where its issued per-connection certificates land (Opc.Gateway.jsonnet gateway.issuedCerts): no
			//split gateway is installed, and a dir that never exists is a warning in every log.
			trustedCertDirs: [ args.certsDir("OpcHub") ]
		}
	},
	logging+:{ spd+:{ sinks+:{ file+: args.logFile + { path: args.logsDir }, console+: args.logConsole } } } //keep the previous starts' logs beside the file, and no source locations in the console (args/install).
}
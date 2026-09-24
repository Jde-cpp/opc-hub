// Opc.Server.jsonnet as installed by apps/OpcHub/setup (`-settings=<this file> -include=args/install`).  The base config is
// dev-shaped wherever it reaches outside the product dir - nodesets under $(UA_NODE_SETS) and the repo, a build-target
// resource and credentials name, the split AppServer's cert as the login TLS anchor - and each of those is replaced from
// the install args here; everything else (ports 1970/4840, the registry it logs in to on 1967, pubsub) is unchanged.  An
// overlay like Opc.Server.Hub.jsonnet: jsonnet is lazy, so the base's args.buildTarget/args.repoSourceDir reads are never
// forced once shadowed, and args/install need not define them.
local args = import 'args.libsonnet';
local base = import 'Opc.Server.jsonnet';
//the interface both listeners bind: null (args/install) is every interface; args/install-user - the current-user mode - is
//loopback, so Windows raises no firewall prompt for a profile install and the server answers its own machine only
//(install-issues #16).  /http/address is the web server's; /opcServer/address the UA endpoint's (UAConfig.cpp).
local listen = if std.objectHas(args, 'listenAddress') then args.listenAddress else null;
function( sync=false ) base( sync ) + {
	http+: { address: listen },
	opcServer+: {
		resource: "install",
		address: listen,
		configFiles: [
			args.nodesetsDir+"/Opc.Ua.Di.NodeSet2.xml", //OPC Foundation UA-Nodeset DI/IA - bundled by the installer.
			args.nodesetsDir+"/Opc.Ua.IA.NodeSet2.xml",
			args.nodesetsDir+"/Opc.Ua.IA.NodeSet2.examples.xml",
			args.nodesetsDir+"/pumps.NodeSet2.xml" //the PLC emulator's tags - urn:jde:pumps
		]
	},
	//Not the login identity:  nothing reads /credentials past main.cpp's Settings::AsObject, which only requires the object to
	//exist.  The login is the /http/ssl certificate (libs/app/client/appClient.cpp getJwt: a JWT signed with its key, the user's
	//name and slug from the cert's CN - args.instanceName+".web", "OpcServer.web" here), admitted through the hub's
	//trustedCertDirs anchor on certsDir("OpcServer").  Overridden only because the base value reads args.buildTarget, which
	//args/install does not define - a shadowed read is never forced (the header).
	credentials+: { name: "OpcServer" },
	//the hub, by ip: `localhost` resolves to ::1 and then 127.0.0.1, and Windows takes ~2 s to refuse each while the hub is not
	//up yet, which doubled the retry's cost (install-issues, the 09-15 rerun's retry table); the hub's certificate names
	//IP:127.0.0.1, so the login's host check passes, and it is the address a current-user hub binds (args/install-user).
	server+: { host: "127.0.0.1" },
	logging+: { spd+: { sinks+: { file+: args.logFile } } }, //keep the previous starts' logs beside the file (args/install).
	web+:{ client+:{ ssl+:{ caFile: args.certsDir("OpcHub")+"/OpcHub.pem" } } } //the hub is the registry - Opc.Hub.jsonnet's /http commonName.
}

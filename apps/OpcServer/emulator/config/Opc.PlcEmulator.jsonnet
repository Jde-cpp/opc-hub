// PLC emulator - no database.  Logs into the AppServer (:1967) for a session (its id is the OPC UA issued token), runs its
// own headless UA server holding the pumps nodeset, PUBLISHES the pump process values over OPC UA PubSub into the
// OpcServer (:4840 tcp / the contract's udp url), and keeps a client session on the OpcServer for the run commands the
// UI writes (-transport=write routes every tag over that session instead and skips the PLC server).
//
// The pubsub transport needs the OpcServer started with config/Opc.Server.Emulator.jsonnet (…Emulator.Hub.jsonnet against
// a hub) - the stock config carries no /opcServer/pubsub, because a DataSetReader writes the address space with no session
// and no access control.  -transport=write needs no overlay; it is an ordinary authenticated client session.
//
// First run: -createCert, then -grant once OpcServer has booted, then restart OpcServer (it loads acls at startup).
// Launch with -tests from $buildDir/runtime; -url= -transport= -period= -statusPeriod= -duration= -opcSchema= beat the settings.
local logsDir = std.extVar("logsDir");
local buildTarget = std.extVar("buildTarget");
local pubsub = import '../../config/pubsub/pumps.libsonnet';
// Two uris, two jobs.  applicationUri is THIS device's identity: what the client advertises (clientDescription) and the
// SAN of its channel certificate - the OpcServer matches those two against each other (open62541 validateCertificate),
// never against its own uri.  serverApplicationUri is the endpoint FILTER (open62541 matchEndpoint): with one set the
// client takes only endpoints whose server advertises it; the Jde OpcServer's is its certificate's SAN, open62541's
// default here; "" skips the filter.  The gateway derives all three from one per-connection certificateUri
// (UAClient::Configuration), so a Jde client claims to BE the server it talks to - this device says who it is.
local applicationUri = "urn:jde:plc-emulator";
local serverApplicationUri = "urn:open62541.server.application";
{
	//Two layers talk to the AppServer.  The LOGIN is an HTTPS request (ClientHttpAwait is always TLS) presenting the
	//http.ssl key below - the caFile anchor is for that handshake.  The SESSION it opens is the app socket, and that is
	//all isSsl selects (appClient.cpp IsSsl): ws:// against the plain AppServer here, wss:// against one serving TLS.
	server:{ host: "localhost", port: 1967, isSsl: false },
	//The login is by key, not by name: enrollment names the user from this certificate (LoginAwait: UPN, email, else the
	//CN - "PlcEmulator.debug.webServer" for the debug build), and -grant grants that session's user.  There is no
	//user-name setting - the `credentials.name` that used to sit here was read by nothing.
	http:{ ssl:{ productName: "PlcEmulator" } }, //keys under $(companyDir)/PlcEmulator/ssl; -createCert issues it.
	//the AppServer is its own root; without this anchor the login TLS handshake rejects its self-signed cert (as the OpcServer/gateway configs anchor it).
	web:{ client:{ ssl:{ caFile: "$(ProgramData)/Jde-Cpp/AppServer/ssl/certs/AppServer.pem" } } },
	emulator:{
		transport: "pubsub", //pubsub | write
		//Verify the OpcServer's certificate against trustedCertDirs before opening the session (jde/opc/ServerTrust.h), as the
		//gateway does with /gateway/verifyServerCertificate and /gateway/trustedCertDirs.
		verifyServerCertificate: true,
		//The OPC servers this PLC will talk to, one .pem/.crt per server, read on every connect - the gateway's verifier, on a
		//list of this app's own.  A Jde OpcServer on this host publishes its own here; for any other server copy its
		//certificate into a directory named here.  Not the OS root store - OPC server certificates are self-signed.
		trustedCertDirs: [ "$(ProgramData)/Jde-Cpp/OpcServer/ssl/certs" ],
		url: "opc.tcp://127.0.0.1:4840",
		applicationUri: applicationUri,
		serverApplicationUri: serverApplicationUri,
		opcSchema: "opc."+buildTarget, ///opcServer/resource = args.buildTarget -> the acl schema.
		plc:{
			port: 4841, //the emulated PLC's own UA endpoint - headless, but a UA_Server must bind something.
			//Listen host.  open62541's setMinimal would bind every interface with anonymous full access over a writable
			//nodeset, i.e. a second unauthenticated door onto the pump tags; nothing is meant to connect here, so loopback.
			//"" = every interface, for a deliberately LAN-visible demo PLC - the emulator WARNs when it is set that way.
			bind: "127.0.0.1",
			nodeset: "$(JDE_DIR)/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml" //the same file the OpcServer loads.
		},
		pubsub: pubsub,
		period: "PT1S",
		statusPeriod: "PT1M",
		reconnectMin: "PT1S",
		reconnectMax: "PT30S",
		ssl:{ //the UA channel cert; same product dir as the login cert, so one trustedCertDirs entry covers it.
			productName: "PlcEmulator",
			//the SAN must be the applicationUri advertised above - a changed uri re-issues on the next start (EnsureKeyCertificate).
			certificate:{ commonName: "PlcEmulator.opc", subjectAltName: "URI:"+applicationUri, country: "US" }
		},
		//path = browse path under Objects (segments '/'-separated), tag names = browse names below it.  Both resolve in the
		//contract's namespace (pubsub.dataSet.namespace, urn:jde:pumps) unless a segment says otherwise with `<index>~name`
		//- the `pumps~` alias the contract's own field paths use is the contract's convention, not needed here.  A tag
		//named in the contract ("<device>.<tag>") is published; command tags are subscribed; everything else is written
		//over the session.
		devices:[
			{ path: "pump1", tags:[ { name: "motorRpm", mode: "follow", ratedRpm: 1450, tau: "PT3S" }, { name: "status", mode: "command" } ] },
			{ path: "pump2", tags:[ { name: "motorRpm", mode: "sine", min: 800, max: 1600, period: "PT30S" }, { name: "status", mode: "toggle", period: "PT15S" } ] },
			{ path: "pump3", tags:[ { name: "motorRpm", mode: "ramp", min: 0, max: 1450, period: "PT20S" }, { name: "status", mode: "command" } ] },
			{ path: "pump4", tags:[ { name: "motorRpm", mode: "randomWalk", min: 1300, max: 1500, step: 10 }, { name: "status", mode: "command" } ] },
			{ path: "pumpManual", tags:[ { name: "motorRpm", mode: "counter", min: 0, max: 100000, step: 1 } ] }
		]
	},
	logging:{
		spd:{
			tags:{ default: "Information", uaNet: "Warning", uaClient: "Warning", uaSecure: "Warning", uaSession: "Warning", uaServer: "Warning", uaPubSub: "Information" },
			sinks:{ console:{}, file:{ path: logsDir, md: false } }
		}
	},
	workers:{ drive:{ threads: 1 }, executor:{ threads: 2 } }
}

// PLC emulator - no database.  Logs into the AppServer (:1967) for a session (its id is the OPC UA issued token, and the
// hub its own server's sessions log in through), runs its own UA server holding the pumps nodeset on :4842, PUBLISHES the
// pump process values over OPC UA PubSub into the OpcServer (:4840 tcp / the contract's udp url), and keeps a client
// session on the OpcServer for the run commands the UI writes (-transport=write routes every tag over that session
// instead and skips the PLC server;  -transport=server keeps the PLC server alone, for a gateway group to collect from).
// Beside the pumps under every transport, a tightening station on Open Protocol's port, :4545 (/emulator/tightening).
//
// The pubsub transport needs the OpcServer started with config/Opc.Server.Emulator.jsonnet (…Emulator.Hub.jsonnet against
// a hub) - the stock config carries no /opcServer/pubsub, because a DataSetReader writes the address space with no session
// and no access control.  -transport=write needs no overlay; it is an ordinary authenticated client session.
// -transport=server needs no OpcServer at all:  the gateway connects to /emulator/plc as to a PLC.
//
// First run: -createCert, then -grant once OpcServer has booted, then restart OpcServer (it loads acls at startup).
// Launch with -tests from $buildDir/runtime; -url= -transport= -period= -statusPeriod= -duration= -opcSchema= beat the settings.
local logsDir = std.extVar("logsDir");
local buildTarget = std.extVar("buildTarget");
local pubsub = import '../../config/pubsub/pumps.libsonnet';
// Two uris, two jobs.  applicationUri is THIS device's identity: what the client advertises (clientDescription) and the
// SAN of its channel certificate - the OpcServer matches those two against each other (open62541 validateCertificate),
// never against its own uri.  serverApplicationUri is the endpoint FILTER (open62541 matchEndpoint): with one set the
// client takes only endpoints whose server advertises it; the Jde OpcServer's is its certificate's SAN
// (Opc.Server.jsonnet), urn:<its host>:Jde-Cpp:OpcServer, this box's here; "" skips the filter.  The gateway derives all
// three from one per-connection certificateUri (UAClient::Configuration), so a Jde client claims to BE the server it talks
// to - this device says who it is.
local applicationUri = "urn:jde:plc-emulator";
local serverApplicationUri = "urn:$(HostName):Jde-Cpp:OpcServer";
// production's figures (README, the modes):  the pumps' counts and the tightening station's rundowns run on the one set.
local production = { cycle: "PT2S", jitter: "PT0.5S", reject: 0.05 };
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
		transport: "pubsub", //pubsub | write | server
		//Verify the OpcServer's certificate against trustedCertDirs before opening the session (jde/opc/ServerTrust.h), as the
		//gateway does with /gateway/verifyServerCertificate and /gateway/trustedCertDirs.
		verifyServerCertificate: true,
		//The OPC servers this PLC will talk to, one certificate per server (.pem, .crt, .der or .cer), read on every connect -
		//the gateway's verifier, on a
		//list of this app's own.  A Jde OpcServer on this host publishes its own here; for any other server copy its
		//certificate into a directory named here.  Not the OS root store - OPC server certificates are self-signed.
		trustedCertDirs: [ "$(ProgramData)/Jde-Cpp/OpcServer/ssl/certs" ],
		url: "opc.tcp://127.0.0.1:4840",
		applicationUri: applicationUri,
		serverApplicationUri: serverApplicationUri,
		opcSchema: "opc."+buildTarget, ///opcServer/resource = args.buildTarget -> the acl schema.
		namespace:: pubsub.dataSet.namespace, //the device paths' default namespace (devices[].path);  absent:  the contract's, which the write and server transports read for nothing else
		//The PLC's own UA endpoint, on the UA server library's security - the OpcServer's policies, a trust list, and a login
		//through the hub this process is logged into (docs/servers/spec.md *Emulator*):  what a gateway group collects from under
		//-transport=server, and what the writer samples under pubsub.  4841 is the gateway's UA endpoint (hub or standalone),
		//4840 the OpcServer's.  A `::` key is its default, shown hidden.
		plc:{
			port: 4842,
			address:: "127.0.0.1", //the interface it listens on:  loopback, a PLC only this box's gateway dials;  "" every interface, for a LAN-visible demo PLC (Windows asks its firewall on each run)
			nodeset: "$(JDE_DIR)/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml", //the same file the OpcServer loads.
			//absent:  the endpoint presents the device's own pair, /emulator/ssl below, whose URI SAN is the applicationUri - one identity, client or server
			ssl:: $.emulator.ssl,
			//the UA clients admitted on a secured channel and as certificate tokens:  the gateway's issued per-connection certificates
			//(Opc.Gateway.jsonnet gateway.issuedCerts) land in its product dir - the standalone gateway's or the hub's.  The gateway
			//trusts this device's pair in return (gateway.trustedCertDirs).  Not the login's anchors, which are the hub's (/access).
			trustedCertDirs: [ "$(ProgramData)/Jde-Cpp/OpcGateway/ssl/certs", "$(ProgramData)/Jde-Cpp/OpcHub/ssl/certs" ],
			rejectedCertDir:: "$(ProgramData)/Jde-Cpp/PlcEmulator/ssl/rejected", //where a refused client certificate is written, <thumbprint>.der, for the admin to move into a trusted dir
			none:: true, //a None endpoint beside the secured ones;  false keeps the None policy for discovery alone (securityPolicyNoneDiscoveryOnly)
			tokenTypes:: { anonymous: false, certificate: true, issued: true, username: true }, //a session is a hub user:  the gateway's collector presents its hub session as an issued token
			users:: [], //the user-name logins, [ { name: "operator", password: "$(OPERATOR_PASSWORD)" } ], plain text at rest;  the login's hub user is the OpcServer-provider user of that name
			userTokenPolicyUri:: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", //the policy the user tokens ride on;  "" names each security policy for itself
			//open62541's own limits (0 unlimited where it reads so);  absent keeps them.  Every gateway user browsing this PLC holds a
			//session of its own beside the collector's, so a busy demo raises the first two;  maxSecureChannels is never left below maxSessions.
			limits:: { maxSessions: 100, maxSecureChannels: 100, maxMonitoredItems: 0, sessionTimeout: "PT1H", minSamplingInterval: "PT0.05S", maxAsyncOperations: 0 }
		},
		pubsub: pubsub,
		//A tightening station on Open Protocol (docs/open-protocol/spec.md *Emulator*):  a Power Focus 6000 named `Jde PLC emulator`,
		//for a gateway's connection of kind open-protocol, url open-protocol://127.0.0.1:4545.  A rundown every `cycle`, give or take
		//`jitter`, while its tool is enabled, a `reject` share of them NOK by a torque outside the selected set's limits, the OK
		//ones counted to the set's `batchSize` and over again, each result pushed and kept for MID 0064.  SelectPset takes a set
		//of `psets`, DisableTool and EnableTool stop and start the rundowns, and SelectJob and DownloadVin are refused.  Without
		//the block there is no station.  A `::` key is its default, shown hidden.
		tightening:{
			port:: 4545, //Open Protocol's own
			address:: "127.0.0.1", //the interface it listens on:  loopback;  "" every interface, for a gateway on another box
			cycle: production.cycle, //PT1S where left out, as a production tag's
			jitter: production.jitter, //none where left out
			reject: production.reject, //0 where left out
			//The first is selected at the start.  A torque is in Nm, the target inside its limits;  a batchSize of 0, or none, is no batch.
			psets:[
				{ id: 1, name: "M8 flange", target: 25, min: 23, max: 27, batchSize: 4 },
				{ id: 2, name: "M10 bracket", target: 48, min: 45, max: 51, batchSize: 6 }
			],
			keep:: 10000, //the results kept for MID 0064, the oldest let go past it
			log:: false //every frame in and out at Trace under the openProtocol tag, which /logging/spd/tags must set to Trace
		},
		period: "PT1S",
		zone:: "UTC", //the IANA zone a stop's, slow period's or quality window's `at` is a time of day in
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
		//
		//OEE's rig (docs/oee/spec.md *Emulator*):  `production` counts a unit every `cycle`, give or take `jitter`, while the device's
		//`status` is on, and a `reject` share of them on `rejectTag` too.  pump1 stops when the web's run command stops it, an
		//operator's stop;  pump2's toggle is a stop of fifteen seconds in every thirty, and its `stops` list, {every, for, code}, holds
		//`status` off for the first ten seconds of each minute after the first with `reasonCode` 1, as the toggle would come back
		//on:  so each minute has one stop of fifteen seconds with no code and one of twenty-five, coded from its sixteenth second.
		//The demo's shapes (docs/demo/spec.md *Production*), which the pumps leave out:  a stop is also {mean, min, max, code}, after a gap
		//drawn about `mean` for a length drawn from `min` to `max`, or {at, for, code}, at a time of day in `zone`, its `code` 0 where
		//left out;  a device's `slow` list has the stops' shapes with a `factor`, the cycle that many times as long with `status` on;
		//a device's `seed` repeats its random stops in every run;  and a follow tag's `follows` names the production tag whose pace it
		//lags towards, in units a minute, where `ratedRpm` would be.
		devices:[
			{ path: "pump1", slow:: [], tags:[
				{ name: "motorRpm", mode: "follow", ratedRpm: 1450, tau: "PT3S" },
				production + { name: "totalCount", mode: "production", rejectTag:: "rejectCount" },
				{ name: "status", mode: "command" }
			] },
			{ path: "pump2", stops:[ { every: "PT1M", "for": "PT10S", code: 1 } ], slow:: [], tags:[
				{ name: "motorRpm", mode: "sine", min: 800, max: 1600, period: "PT30S" },
				production + { name: "totalCount", mode: "production", rejectTag:: "rejectCount" },
				{ name: "status", mode: "toggle", period: "PT15S" }
			] },
			{ path: "pump3", tags:[ { name: "motorRpm", mode: "ramp", min: 0, max: 1450, period: "PT20S" }, { name: "status", mode: "command" } ] },
			{ path: "pump4", tags:[ { name: "motorRpm", mode: "randomWalk", min: 1300, max: 1500, step: 10 }, { name: "status", mode: "command" } ] },
			{ path: "pumpManual", tags:[ { name: "motorRpm", mode: "counter", min: 0, max: 100000, step: 1 } ] }
		]
	},
	logging:{
		stack:: { default: "Warning" },//open62541's lines (LogStack):  which walk the stack for their file, line and function.
		spd:{
			tags:{ default: "Information", uaNet: "Warning", uaClient: "Warning", uaSecure: "Warning", uaSession: "Warning", uaServer: "Warning", uaPubSub: "Information" },
			sinks:{ console:{}, file:{ path: logsDir, md: false } }
		}
	},
	workers:{ drive:{ threads: 1 }, executor:{ threads: 2 } }
}
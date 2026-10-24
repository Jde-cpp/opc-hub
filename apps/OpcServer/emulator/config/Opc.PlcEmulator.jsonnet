// PLC emulator - no database.  Logs into the AppServer (:1967) for a session (its id is the OPC UA issued token, and the
// hub its own server's sessions log in through), runs its own UA server holding the pumps nodeset on :4842, PUBLISHES the
// pump process values over OPC UA PubSub into the OpcServer (:4840 tcp / the contract's udp url), and keeps a client
// session on the OpcServer for the run commands the UI writes (-transport=write routes every tag over that session
// instead and skips the PLC server;  -transport=server keeps the PLC server alone, for a gateway group to collect from).
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
		//The PLC's own UA endpoint, on the UA server library's security - the OpcServer's policies, a trust list, and a login
		//through the hub this process is logged into (docs/servers/spec.md *Emulator*):  what a gateway group collects from under
		//-transport=server, and what the writer samples under pubsub.  4841 is the gateway's UA endpoint (hub or standalone),
		//4840 the OpcServer's.  A `::` key is its default, shown hidden.
		plc:{
			port: 4842,
			address:: null, //the interface it listens on;  null:  every interface, as the OpcServer and the gateway;  "127.0.0.1" for a PLC only this box's gateway dials
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
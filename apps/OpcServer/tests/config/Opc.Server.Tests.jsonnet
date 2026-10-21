local args = import 'args.libsonnet';
local logsDir = args.logsDir;
local embeddedAppServer = true; //false = run against a live AppServer on localhost:1967.
//the live services' ports +100 (1967/1970/4840), so the suite runs beside a running AppServer and OpcServer.
local appPort = 2067;
{
	testing:{
		tests:: "*",
		recreateDB: std.parseJson( std.extVar("recreateDB") ),
		embeddedAppServer: embeddedAppServer,
		UANodeSets: "$(UA_NODE_SETS)"
	},
	//a live AppServer is its own root - without this anchor the verify-by-default client rejects its self-signed cert
	//on the first login and the whole suite fails in Opc::Server::Startup.  Only for the live mode: the embedded server's
	//just-generated cert is anchored programmatically in main.cpp, and an unconditional caFile would log a load error
	//on every host with no deployed AppServer.
	[if embeddedAppServer then "server"]: { port: appPort }, //the app client (default 1967) - the live mode keeps it.
	[if !embeddedAppServer then "web"]: {
		client:{ ssl:{ caFile: args.certsDir("AppServer")+"/AppServer.pem" } }
	},
	opc:{
		urn: "urn:open62541.server.application",
		url: "opc.tcp://127.0.0.1:4940"
	},
	access:{
		trustedCertDirs: [
			"$(ProgramData)/Jde-Cpp/$(PRODUCT_NAME)/ssl/certs",
			"$(ProgramData)/Jde-Cpp/OpcServer/ssl/certs" //the app logs into the AppServer with its web cert (Opc::Server::Startup -> SslSettings), and http.opcServer.ssl's productName puts that cert in the OpcServer tree, not $(PRODUCT_NAME)'s.
		]
	},
	opcServer:{
		slug: "TestServer",
		resource: "test",
		description: "Test OPC",
		port: 4940,
		configFiles: [ args.repoSourceDir + "/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml" ],
		hist:{ path: logsDir + "/hist/opc-server-tests", editTimeout: "PT1S" },//HistoryTests clears it, and starves an edit for editTimeout.
		//unicast on a test port: multicast needs a route the CI container lacks, and 4840/udp would collide with a live server.
		//The test host keeps a reader where the stock Opc.Server.jsonnet has none (it is opt-in - an unauthenticated write
		//path, see config/Opc.Server.Emulator.jsonnet); PubSubTests is what covers it, and loopback is not a domain.
		pubsub: (import '../../config/pubsub/pumps.libsonnet') + { url: "opc.udp://127.0.0.1:4849/" },
		//The whole pair in the build dir, so each checkout has its own:  the certificate's default path is the one ProgramData tree
		//every checkout shares, and a certificate issued there on another checkout's key failed every token encrypted to it.
		ssl:{
			certificate:{
				path: args.repoBuildDir + "/OpcServer/ssl/certs/OpcServer.Tests.pem",
				subjectAltName: "URI:urn:open62541.server.application,DNS:localhost,IP:127.0.0.1",
				commonName: args.instanceName + ".opcServer.tests.web",
			},
			privateKey: {path: args.repoBuildDir + "/OpcServer/ssl/private/OpcServer.Tests.pem", passcode: ""},
			publicKey: {path: args.repoBuildDir + "/OpcServer/ssl/public/OpcServer.Tests.pem"}
		}
	},
	dbServers: {
		scriptPaths: [
			args.repoSourceDir + "/apps/AppServer/config/sql/"+args.sqlType,
			args.repoSourceDir + "/libs/access/config/sql/"+args.sqlType
		],
		dataPaths: [args.repoSourceDir + "/apps/AppServer/config", args.repoSourceDir + "/libs/access/config"],
		sync:: false,
		localhost: args.dbServers.localhost
	},
	http:{
		app:{ port: appPort, ssl:{productName: "AppServer", certificate:{ subjectAltName: "DNS:localhost,IP:127.0.0.1" }} },//TLS clients match the SAN - without it host_name_verification rejects the generated cert.
		opcServer:{ port: 2070, ssl:{productName: "OpcServer", certificate:{ subjectAltName: "DNS:localhost,IP:127.0.0.1" }} }
	},
	logging:{
		breakLevel: "Critical",
		spd:{
			tags: {
				default: "Information",
				test: "Trace",
				app: "Trace",
				ql: "Debug",
				settings: "Debug",
				scheduler: "Debug",
				sql: "Debug",
				threads: "Information",
				"http.client.write": "Trace",
				"http.client.read": "Trace",
				"http.server.write": "Debug",
				"http.server.read": "Debug",
				"socket.client.write": "Debug",
				"socket.client.read": "Debug",
				"socket.server.write": "Debug",
				"socket.server.read": "Debug",
				monitoring: "Debug",
				browse: "Debug",
				processingLoop: "Debug",
				uaClient: "Information",
				uaDiscovery: "Information",
				uaEvent: "Debug",
				uaNet: "Information",
				uaPubSub: "Information",
				uaSecure: "Information",
				uaSecurity: "Information",
				uaSession: "Information",
				uaServer: "Information",
				uaUser: "Information"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		memory:{ //kept past Init only with a level under `tags` (log.cpp) - `default` at this depth was dropped at startup.  TrustListTests reads it (Logging::Find).
			tags: { default: "Debug" }
		}
	},
	workers:{
		executor: {threads: 2},
	}
}
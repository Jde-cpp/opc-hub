local args = import 'args.libsonnet';
local logsDir = args.logsDir;
local embeddedAppServer = true; //false = run against a live AppServer on localhost:1967.
{
	testing:{
		tests:: "AccessTests.UserAccess",
		recreateDB: std.parseJson( std.extVar("recreateDB") ),
		embeddedAppServer: embeddedAppServer,
		UANodeSets: "$(UA_NODE_SETS)"
	},
	//a live AppServer is its own root - without this anchor the verify-by-default client rejects its self-signed cert
	//on the first login and the whole suite fails in Opc::Server::Startup.  Only for the live mode: the embedded server's
	//just-generated cert is anchored programmatically in main.cpp, and an unconditional caFile would log a load error
	//on every host with no deployed AppServer.
	[if !embeddedAppServer then "web"]: {
		client:{ ssl:{ caFile: args.certsDir("AppServer")+"/AppServer.pem" } }
	},
	opc: args.opc,
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
		port: 4840,
		configFiles: [ args.repoSourceDir + "/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml" ],
		//unicast on a test port: multicast needs a route the CI container lacks, and 4840/udp would collide with a live server.
		//The test host keeps a reader where the stock Opc.Server.jsonnet has none (it is opt-in - an unauthenticated write
		//path, see config/Opc.Server.Emulator.jsonnet); PubSubTests is what covers it, and loopback is not a domain.
		pubsub: (import '../../config/pubsub/pumps.libsonnet') + { url: "opc.udp://127.0.0.1:4849/" },
		ssl:{
			certificate:{
				subjectAltName: "URI:urn:open62541.server.application,DNS:localhost,IP:127.0.0.1",
				commonName: args.instanceName + ".opcServer.tests.web",
			},
			privateKey: {path: args.repoBuildDir + "/OpcServer/ssl/private/OpcServer.Tests.pem", passcode: ""}
		}
	},
	dbServers: {
		scriptPaths: [
			args.repoSourceDir + "/apps/AppServer/config/sql/"+args.sqlType,
			args.repoSourceDir + "/libs/access/config/sql/"+args.sqlType
		],
		dataPaths: [args.repoSourceDir + "/apps/AppServer/config", args.repoSourceDir + "/libs/access/config"],
		sync:: true,
		localhost:{
			driver: args.dbServers.localhost.driver,
			connectionString: args.dbServers.localhost.connectionString,
			username: args.dbServers.localhost.username,
			password: args.dbServers.localhost.password,
			schema: args.dbServers.localhost.schema,
			catalogs: args.dbServers.localhost.catalogs
		}
	},
	http:{
		app:{ port: 1967, ssl:{productName: "AppServer", certificate:{ subjectAltName: "DNS:localhost,IP:127.0.0.1" }} },//TLS clients match the SAN - without it host_name_verification rejects the generated cert.
		opcServer:{ port: 1970, ssl:{productName: "OpcServer", certificate:{ subjectAltName: "DNS:localhost,IP:127.0.0.1" }} }
	},
	credentials:{
		opcServer:{ name: "OpcTests" }
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
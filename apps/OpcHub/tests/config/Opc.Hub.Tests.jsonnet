// Jde.Opc.Hub.Tests: the hub - both roles behind ONE listener (/http, 1973) - plus an embedded OpcServer (1975, opc.tcp 4842),
// in one process.  Nothing else in the repo binds those ports, so this suite never hangs on a live hub/service (1967/1968/1970)
// or the other suites (1972, 4840), and can run beside a live hub.  /http/app and /http/gateway alias the one port for the two
// apps' test helpers (App::Server::Tests::Port, Gateway::Tests::GatewayPort).
local args = import 'args.libsonnet';
local logsDir = args.logsDir;
{
	local instance = self,
	instanceName: args.instanceName,
	testing:{
		tests:: "*",
		recreateDB: std.parseJson( std.extVar("recreateDB") ),
		embeddedOpcServer:: false
	},
	opc: args.opc + { users: [ { name: "user1", password: "0123456789ABCD" } ] }, //the embedded OpcServer's username login (UAAccess::loadUsers) - HubRoutingTests.LoginDefaultConnection's credential; without a list the server offers no username policy.
	ql:{
		introspection: [ "../../../OpcGateway/config/introspection/serverConnection.jsonnet", "../../../OpcGateway/config/introspection/search.jsonnet" ] //relative to this file (gatewayStartup.cpp).
	},
	access:{
		trustedCertDirs: [
			"$(ProgramData)/Jde-Cpp/$(PRODUCT_NAME)/ssl/certs" //both roles and the embedded OpcServer share the test binary's product dir.
		]
	},
	dbServers: {
		scriptPaths: [
			args.repoSourceDir + "/apps/AppServer/config/sql/"+args.sqlType,
			args.repoSourceDir + "/libs/access/config/sql/"+args.sqlType,
			args.repoSourceDir + "/apps/OpcGateway/config/sql/"+args.sqlType
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
		host: "127.0.0.1", //what /opcGateways advertises; the test clients connect by ip (sessions are endpoint-bound).
		address: "127.0.0.1", //the loopback bind a current-user install uses (args/install-user - install-issues #16): the listener takes it, and the suite still reaches it.
		port: 1973,
		site: args.repoSourceDir + "/apps/OpcHub/tests/config/site", //a stand-in for the built site - HubRoutingTests.SiteServedFromTheHub.
		clientSettings:{
			googleAuthClientId: "opc-hub-tests-google-client-id" //served by GET /GoogleAuthClientId.
		},
		ssl:{
			certificate:{
				subjectAltName: "URI:urn:$(HostName):Jde-Cpp:$(PRODUCT_NAME),DNS:localhost,DNS:$(HostName),IP:127.0.0.1",//the URI: the gateway role's own applicationUri (security-matrix #8) - the gateway role's OPC certificate authentication signs with this cert; the DNS/IP: TLS clients match the SAN - $(HostName) as Opc.Hub.jsonnet carries it (HubRoutingTests.CertificateNamesTheHost).
				commonName: args.instanceName + ".web"
			}
		},
		app:{ port: 1973 },//helpers.h aliases of the one port
		gateway:{ port: 1973, ssl: instance.http.ssl },
		opcServer:{
			port: 1975,
			ssl:{
				certificate:{
					subjectAltName: "URI:urn:open62541.server.application,DNS:localhost,IP:127.0.0.1",
					commonName: args.instanceName + ".opcServer.web"
				}
			}
		}
	},
	server:{ host: "127.0.0.1", port: 1973, isSsl: false }, //the embedded OpcServer's app client - it still logs in over the socket, to the hub's one listener.
	gateway:{
		trustedCertDirs: instance.access.trustedCertDirs, //the embedded OpcServer's UA certificate is in the test product's one ssl/certs; the gateway role reads its own list, not the enrollment anchors (ServerTrust.h).
		issuedCerts: instance.http.gateway.ssl,
		search:{ maxDepth: 12, maxNodes: 25000, limit: 20, includeServer: false }
	},
	opcServer:{
		slug: "TestServer",
		description: "Test OPC",
		configFiles: [
			"$(UA_NODE_SETS)/DI/Opc.Ua.Di.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.examples.xml"
		],
		port: 4842,
		address: "127.0.0.1", //as /http/address: the UA endpoint bound to loopback (UAConfig.cpp applyAddress), reached by HubRoutingTests.LoginDefaultConnection.
		ssl: instance.http.opcServer.ssl
	},
	credentials:{
		name: "HubTests",
		opcServer:{ name: "OpcHubTests.OpcServer" }
	},
	logging:{
		breakLevel: "Critical",
		spd:{
			tags: {
				default: "Information",
				app: "Debug",
				ql: "Information",
				access: "Debug",
				settings: "Debug",
				sessions: "Trace",
				"http.client.write": "Debug",
				"http.client.read": "Debug",
				"http.server.write": "Debug",
				"http.server.read": "Debug",
				"socket.client.read": "Debug",
				"socket.client.write": "Debug",
				"socket.server.read": "Debug",
				"socket.server.write": "Debug",
				test: "Trace",
				sql: "Information",
				opc: "Debug",
				uaClient: "Information",
				uaSecure: "Information",
				uaSession: "Information",
				uaServer: "Information"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		memory:{
			tags:{ default: "Trace" }
		},
		subscribe:{},
		proto:{
			path: logsDir + "/opc-hub-tests",
			timeZone: "America/New_York",
			delay: "PT1M"
		}
	},
	workers:{
		executor: {threads: 2},
		drive: {threads: 1}
	}
}

// Jde.Opc.Hub.Tests: the hub - both roles behind ONE listener (/http, 1973) and its OPC UA endpoint (opc.tcp 4976) - plus an
// embedded OpcServer (1975, opc.tcp 4975), in one process.  Nothing else in the repo binds those ports, so this suite never hangs
// on a live hub/service (1967/1968/1970/4841), the PLC emulator (4842) or the other suites (1972, 2067-2070, 4940/4941), and can
// run beside a live hub.  /http/app and /http/gateway alias the one port for the two apps' test helpers (App::Server::Tests::Port,
// Gateway::Tests::GatewayPort).
local args = import 'args.libsonnet';
local logsDir = args.logsDir;
{
	local instance = self,
	instanceName: args.instanceName,
	testing:{
		tests:: "*",
		recreateDB: std.parseJson( std.extVar("recreateDB") ),
		embeddedOpcServer:: true
	},
	opc: { urn: "urn:open62541.server.application", url: "opc.tcp://" + instance.opcServer.address + ":" + instance.opcServer.port, users: [ { name: "user1", password: "0123456789ABCD" } ] }, //the embedded OpcServer, and its username login (UAAccess::loadUsers) - HubRoutingTests.LoginDefaultConnection's credential; without a list the server offers no username policy.
	ql:{
		introspection: [ "../../../OpcGateway/config/introspection/serverConnection.jsonnet", "../../../OpcGateway/config/introspection/search.jsonnet", "../../../OpcGateway/config/introspection/hist.jsonnet" ] //relative to this file (gatewayStartup.cpp).
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
		sync:: false,
		localhost: args.dbServers.localhost
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
		search:{ maxDepth: 12, maxNodes: 25000, limit: 20, includeServer: false },
		hist:{ path: logsDir + "/hist/opc-hub-tests" }, //the gateway role's historian:  its own path, so a gateway test run beside this one never contends for the lock.
		//the hub's own endpoint (EndpointTests):  loopback as the listener is, the process pair (no ssl), and a hub-shape login - a short
		//issued token answered from this process's session map (HubAppClient::SessionInfoAwait).
		server:{
			port: 4976,
			address: instance.http.address,
			trustedCertDirs: instance.access.trustedCertDirs,
			rejectedCertDir: logsDir + "/rejected/opc-hub-tests"
		}
	},
	opcServer:{
		slug: "TestServer",
		description: "Test OPC",
		configFiles: [
			"$(UA_NODE_SETS)/DI/Opc.Ua.Di.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.examples.xml"
		],
		port: 4975,
		address: "127.0.0.1", //as /http/address: the UA endpoint bound to loopback (UAConfig.cpp applyAddress), reached by HubRoutingTests.LoginDefaultConnection.
		ssl: instance.http.opcServer.ssl
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
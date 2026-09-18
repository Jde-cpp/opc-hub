local args = import 'args.libsonnet';
local logsDir = args.logsDir;
{
	local instance = self,
	instanceName: args.instanceName,
	testing:{
		tests:: "QLTests.ServerDescriptionTest",
		recreateDB: std.parseJson( std.extVar("recreateDB") ),
		embeddedAppServer:: false,
		embeddedOpcServer:: false
	},
	opc: args.opc + { users: [ { name: "user1", password: "0123456789ABCD" } ] }, //the embedded OpcServer's username login (UAAccess::loadUsers) - PasswordTests' credential; without a list the server offers no username policy and the Auth fixture skips them.
	ql:{
		introspection: [ "../../config/introspection/serverConnection.jsonnet", "../../config/introspection/search.jsonnet" ] //relative to this file, as in Opc.Gateway.jsonnet: the extensions QLTests asserts on.
	},
	access:{
		trustedCertDirs: [
			"$(ProgramData)/Jde-Cpp/$(PRODUCT_NAME)/ssl/certs"
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
	iot: {
		slug: "Default"
	},
	http:{
		app:{
			port: 1967,
			ssl:{
				certificate:{
					subjectAltName: "DNS:localhost,IP:127.0.0.1",//TLS clients match the SAN - without it host_name_verification rejects the generated cert.
					commonName: args.instanceName + ".appServer.web"
				}
			}
		},
		gateway:{
			port: 1968,
			ssl:{
				certificate:{
					subjectAltName: "URI:urn:$(HostName):Jde-Cpp:$(PRODUCT_NAME),DNS:localhost,IP:127.0.0.1", //the URI: the gateway's own applicationUri - gateway.issuedCerts, below, is this block - where the opcServer's, next, is the server's (security-matrix #8).
					commonName: args.instanceName + ".gateway.web"
				}
			}
		},
		opcServer:{
			port: 1970,
			ssl:{
				certificate:{
					subjectAltName: "URI:urn:open62541.server.application,DNS:localhost,IP:127.0.0.1",
					commonName: args.instanceName + ".opcServer.web"
				}
			}
		}
	},
	gateway:{
		trustedCertDirs: instance.access.trustedCertDirs, //the embedded OpcServer's UA certificate is issued into the test product's one ssl/certs - the same directory the embedded AppServer anchors enrollment on, but a list of its own (ServerTrust.h).
		issuedCerts: instance.http.gateway.ssl,
		allowPlaintextPassword: false, //the shipped default; PlaintextPasswordTests flips it for one test.
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
		port: 4840,
		ssl: instance.http.opcServer.ssl
	},
	credentials:{
		gateway:{ name: "GatewayTests" },
		opcServer:{ name: "OpcTests" }
	},
	logging:{
		loadFromServer: false,
		breakLevel: "Critical",
		spd:{
			tags: {
				default: "Information",
				app: "Debug",
				ql: "Information",
				settings: "Debug",
				scheduler: "Debug",
				"http.client.write": "Debug",
				"http.client.read": "Debug",
				"http.server.write": "Debug",
				"http.server.read": "Debug",
				locks: "Information",
				"socket.client.read": "Debug",
				"socket.client.read.subscription": "Information",
				"socket.client.write": "Debug",
				"socket.client.write.subscription": "Information",
				"socket.server.read": "Debug",
				"socket.server.write": "Debug",
				test: "Trace",
				threads: "Information",
				processingLoop: "Trace",
				sql: "Information",
				browse: "Information",
				monitoring: "Information",
				opc: "Debug",
				uaClient: "Information",
				uaDiscovery: "Information",
				uaEvent: "Information",
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
		memory:{
			default: "trace"
		},
		proto:{
			path: logsDir + "/opc-tests",
			timeZone: "America/New_York",
			delay: "PT1M"
		},
		// subscribe:{},
		// remote:{
		// 	delay: "PT0.1S"
		// }
	},
	workers:{
		executor: {threads: 2},
		drive: {threads: 1}
	}
}
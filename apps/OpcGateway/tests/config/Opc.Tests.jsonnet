local args = import 'args.libsonnet';
local logsDir = args.logsDir;
{
	local instance = self,
	instanceName: args.instanceName,
	testing:{
		recreateDB: std.parseJson( std.extVar("recreateDB") ),
		embeddedAppServer:: true,
		embeddedOpcServer:: true,
		openProtocolPort:: 4645 //the test controller's (DriverRowTests, DriverHostTests):  a controller's 4545 plus 100, as the ports below are.
	},
	opc: { urn: "urn:open62541.server.application", url: "opc.tcp://127.0.0.1:4940", users: [ { name: "user1", password: "0123456789ABCD" } ] }, //the embedded OpcServer, and its username login (UAAccess::loadUsers) - PasswordTests' credential; without a list the server offers no username policy and the Auth fixture skips them.
	ql:{
		introspection: [ "../../config/introspection/serverConnection.jsonnet", "../../config/introspection/search.jsonnet", "../../config/introspection/hist.jsonnet", "../../config/introspection/oee.jsonnet", "../../config/introspection/flows.jsonnet" ] //relative to this file, as in Opc.Gateway.jsonnet: the extensions QLTests asserts on.
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
		sync:: false,
		localhost: args.dbServers.localhost
	},
	iot: {
		slug: "Default"
	},
	//the live services' ports +100 (1967/1968/1970/4840), so the suite runs beside a running AppServer, gateway and OpcServer.
	http:{
		app:{
			port: 2067,
			ssl:{
				certificate:{
					subjectAltName: "DNS:localhost,IP:127.0.0.1",//TLS clients match the SAN - without it host_name_verification rejects the generated cert.
					commonName: args.instanceName + ".appServer.web"
				}
			}
		},
		gateway:{
			port: 2068,
			ssl:{
				certificate:{
					subjectAltName: "URI:urn:$(HostName):Jde-Cpp:$(PRODUCT_NAME),DNS:localhost,IP:127.0.0.1", //the URI: the gateway's own applicationUri - gateway.issuedCerts, below, is this block - where the opcServer's, next, is the server's (security-matrix #8).
					commonName: args.instanceName + ".gateway.web"
				}
			}
		},
		opcServer:{
			port: 2070,
			ssl:{
				certificate:{
					subjectAltName: "URI:urn:open62541.server.application,DNS:localhost,IP:127.0.0.1",
					commonName: args.instanceName + ".opcServer.web"
				}
			}
		}
	},
	calendars:{ retryWait: "PT0.5S" }, //CalendarClientTests.ARefusedReadHoldsNothing waits on it.
	server:{ port: instance.http.app.port }, //the gateway's and embedded OpcServer's app client (default 1967).
	//the embedded OpcServer's UA certificate is issued into the test product's one ssl/certs - the same directory the embedded
	//AppServer anchors enrollment on, but a list of its own (ServerTrust.h) - and a servers dir of its own beside it (under logsDir,
	//so a run cleans up after itself), where trustServer moves a trusted certificate:  not the list's first entry, the enrollment
	//anchors, where every certificate may create a user (#415).  The rejected directory is its default, serversDir's
	//rejected-servers child.
	local serversDir = logsDir + "/servers/opc-gateway-tests",
	gateway:{
		trustedCertDirs: instance.access.trustedCertDirs + [ serversDir ],
		serversDir: serversDir,
		issuedCerts: instance.http.gateway.ssl,
		allowPlaintextPassword: false, //the shipped default; PlaintextPasswordTests flips it for one test.
		//Not the shipped values, which the suite would wait out:  a push waits up to a publishing interval at the server (about 18 s of
		//the suite at 0.5 s), most tests wait for their items' delete after the unsubscribe, and a reconnect for its first delay.  A test
		//built on the shipped 1 s window sets it back for its run (utils/helpers.h DurationOverride).
		subscription: { publishingInterval: "PT0.1S", itemDeleteDelay: "PT0.1S" },
		reconnectDelay: "PT0.1S",
		search:{ maxDepth: 12, maxNodes: 25000, limit: 20, includeServer: false },
		hist:{ path: logsDir + "/hist/opc-gateway-tests" }, //the gateway's own groups (HistGroupTests), which main.cpp empties at the start as it does the server's.
		flows:{ path: logsDir + "/flows/opc-gateway-tests" }, //FlowTests:  a flow's trace directory, which its purge removes;  main.cpp empties it at the start.
		oee:{ live:: "PT1S", settle: "PT2S" }, //OeeTests:  not the shipped 5 s, so OeeTests.TheSettingsReachTheHost tells the setting from the default;  the suite's clock is its own, so nothing waits on it.
		//the gateway's own endpoint (GatewayServerTests), beside the embedded OpcServer's 4940:  two UA servers in one process.  No ssl:
		//the process pair, http.gateway.ssl.  Its client certificates are the test product's one ssl/certs, where that pair is - the suite's
		//client logs in by certificate with it - and user1 is the OpcServer's login too.
		server:{
			port: 4941,
			trustedCertDirs: instance.access.trustedCertDirs,
			rejectedCertDir: logsDir + "/rejected/opc-gateway-tests",
			users: instance.opc.users,
			limits: { maxSecureChannels: 120 } //UASettings::ReadLimits, the emulator's reader too (GatewayServerTests.TheBlockIsReadAndTheEndpointRuns)
		},
		//a driver row's server, at 4980 plus its row's id (DriverRowTests, DriverHostTests):  past the shipped 4870 range and the hub
		//suite's 4975 and 4976.  The link's waits are not the shipped ones, which a case that stops its controller would wait out.
		drivers:{ portBase: 4980, openProtocol:{ reconnectMin: "PT0.1S", reconnectMax: "PT0.5S" } }
	},
	opcServer:{
		slug: "TestServer",
		description: "Test OPC",
		configFiles: [
			"$(UA_NODE_SETS)/DI/Opc.Ua.Di.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.examples.xml",
			args.repoSourceDir + "/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml" //last, so the namespaces the other tests name keep their indexes:  the historized nodes HistTests reads through the gateway.
		],
		hist:{ path: logsDir + "/hist/opc-tests" },//the embedded server's own history, which main.cpp empties at the start.
		port: 4940,
		ssl: instance.http.opcServer.ssl
	},
	logging:{
		loadFromServer: false,
		breakLevel: "Critical",
		stack:: { default: "Warning" },//open62541's lines (LogStack):  which walk the stack for their file, line and function.
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
		memory::{ tags: { default: "None" } },//the suite's capture (jde/tests/Capture.h), off:  a test captures what it reads with a Logging::Capture.
		proto:{
			path: logsDir + "/opc-tests",
			timeZone: "America/New_York",
			delay: "PT1M",
			file:: { default: "Warning", exception: "Trace" },//which entries keep their file and line in the log files, a logger's `tags` shape:  warnings, errors and exceptions.
			"function":: { default: "None", exception: "Trace" },//...and their function:  exceptions.
		}
	},
	workers:{
		executor: {threads: 2},
		drive: {threads: 1}
	}
}
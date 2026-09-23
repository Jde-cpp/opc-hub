local args = import 'args.libsonnet';
local logsDir = args.logsDir;
function( sync=false )
{
	instanceName: args.instanceName,
	dbServers:{
		dataPaths: args.dbServers.dataPaths,
		scriptPaths: args.dbServers.scriptPaths,
		sync: sync,
		localhost:{
			driver: args.dbServers.localhost.driver,
			connectionString: args.dbServers.localhost.connectionString,
			username: args.dbServers.localhost.username,
			password: args.dbServers.localhost.password,
			schema: args.dbServers.localhost.schema,
			catalogs: args.dbServers.localhost.catalogs
		}
	},
	logging:{
		spd:{
			flushOn: "Trace",
			tags: {
				default: "Information",
				test: "Trace",
				app: "Debug",
				io: "Warning",
				ql: "Debug",
				settings: "Debug",
				"http.client.read": "Debug",
				"http.client.write": "Debug",
				"socket.client.read": "Debug",
				"socket.client.write": "Debug",
				sql: "Information",
				threads: "Warning",
				"opc.access": "Trace",
				uaEvent: "Information",
				uaNet: "Information",
				uaSession: "Information",
				uaServer: "Information",
				uaUser: "Information",
				uaSecurity: "Information",
				uaClient: "Information",
				uaSecure: "Information"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		proto:{//without this ProtoLog::Init is a no-op, there is no binary archive, and the `logs` query 500s on GetLogger<ProtoLog> - which is what the web ui's Logs tab reads.
			path: logsDir + "/opc-server",
			//no timeZone:  the archive's day directories (<path>/<yyyy>/<m>/<d>) follow the machine's zone, ProtoLog's default - a developer's "America/New_York" here shipped to every install (reviews/m4-closing.md #14).  An IANA name pins them.
			delay: "PT1M",
			tags: {
				default: "Debug",
				externalLogger: "None"
			}
		}
	},
	credentials:{
		name: "OpcServer.Test."+args.buildTarget,
		slug:: "OpcServer"
	},
	web:{
		client:{ ssl:{ caFile: args.certsDir("AppServer")+"/AppServer.pem" } }//the AppServer is its own root - without an anchor the client rejects localhost:1967's self-signed cert.  The stem is App.Server.jsonnet's visible commonName; if that or its `path::` un-hides, this anchor must follow.
	},
	http:{
		host: "localhost",//advertised to the AppServer registry - the frontend fetches this host, and allowOrigin 'sameHost' requires it to match the page's host (localhost:4200).
		port: 1970,
		ssl:{
			certificate:{
				commonName: args.instanceName + ".web"
			},
			privateKey:{ passcode: "$(JDE_PASSCODE)" }
		}
	},
	opcServer:{
		slug: "TestServer",
		resource: args.buildTarget,
		description: "Test OPC",
		ssl:{
			certificate: {
				path:: "{ApplicationDataFolder}/ssl/certs/OpcServer.pem",
				subjectAltName: "URI:urn:open62541.server.application",
				company:: "Jde-Cpp",
				country: "US",
			},
			privateKey: {
				path:: "{ApplicationDataFolder}/ssl/private/OpcServer.pem",
				passcode: "$(JDE_PASSCODE)" //encrypts the key at rest; empty/unset = cleartext (the startup log says which). Change only with the key deleted.
			},
			publicKey:{
				path:: "{ApplicationDataFolder}/ssl/public/OpcServer.pem"
			},
			dh:: "{ApplicationDataFolder}/ssl/dh.pem",
		},
		configFiles: [
			//"$(JDE_DIR)/apps/OpcServer/config/nodesets/uaPredefinedNodes.xml",
			"$(UA_NODE_SETS)/DI/Opc.Ua.Di.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.xml",
			"$(UA_NODE_SETS)/IA/Opc.Ua.IA.NodeSet2.examples.xml",
			args.repoSourceDir + "/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml" //the PLC emulator's tags - urn:jde:pumps
		],
		//No `pubsub` key here, deliberately.  A DataSetReader writes its target variables through the server-internal path -
		//no session, no OpcAuthorize - so any UADP publisher that can reach the url and carries the contract's three ids
		//drives those nodes.  That is a demo affordance, not a production one: the PLC-emulator overlays
		//(Opc.Server.Emulator.jsonnet, Opc.Server.Emulator.Hub.jsonnet) add it, and PubSub::Reader WARNs when they do.
		port: 4840
	},
	//the UA server's trust list.  UAConfig reads /access/trustedCertDirs, not /opcServer/trustedCertDirs - anchoring it
	//under opcServer leaves the server with zero anchors and every secured client rejected BadCertificateUntrusted.
	access:{
		trustedCertDirs: args.access.trustedCertDirs
	},
	workers:{
		executor:{ threads:  2 },
		drive:{ threads:  2 }
	}
}
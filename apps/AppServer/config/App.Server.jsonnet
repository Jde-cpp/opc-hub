local args = import 'args.libsonnet';
local logsDir = args.logsDir;
function( sync=false )
{
	http:{
		address: null,
		port: 1967,
		threads: 1,
		timeout: "P1D",
		socketTimeout:: "P1D",
		maxLogLength: 255,
		bodyLimit: 10000,
		accessControl:{
			allowOrigin: "sameHost",
			allowMethods: "GET, POST, OPTIONS",
			allowHeaders: "Content-Type, Authorization"
		},
		clientSettings:{
			googleAuthClientId:"445012155442-1v8ntaa22konm0boge6hj5mfs15o9lvd.apps.googleusercontent.com"
		},
		ssl:{
			certificate:{
				path:: "{ApplicationDataFolder}/ssl/certs/AppServer.http.pem",
				subjectAltName: "DNS:localhost,IP:127.0.0.1",
				company:: "Jde-Cpp",
				country: "US",
				commonName: "AppServer"
			},
			privateKey:{
				path:: "{ApplicationDataFolder}/ssl/private/AppServer.http.pem",
				passcode: "$(JDE_PASSCODE)"
			},
			publicKey:{
				path:: "{ApplicationDataFolder}/ssl/public/AppServer.http.pem"
			},
			dh:{
				path:: "{ApplicationDataFolder}/ssl/dh.pem"
			},
		},
	},
	access:{
		//operator drop-dir for enrollment trust anchors: a client cert (.pem, .crt, .der or .cer - either encoding parses)
		//copied here authorizes its key-login enrollment. Rescanned on failed verification - no restart needed.
		//Production products only.  Every cert under these dirs can enroll a user whose identity is the cert's CN, so a
		//test/dev product dir here would let anything that writes one provision an account in the production access db;
		//the test binaries anchor their own dirs in their own configs (Opc.Server.Tests.jsonnet, Opc.Tests.jsonnet).
		trustedCertDirs: [
			"$(ProgramData)/Jde-Cpp/OpcServer/ssl/certs",
			"$(ProgramData)/Jde-Cpp/OpcGateway/ssl/certs",
			"$(ProgramData)/Jde-Cpp/PlcEmulator/ssl/certs" //the PLC emulator (apps/OpcServer/emulator) logs in for a session that becomes its OPC UA issued token; like the gateway, a first-class app, not a test binary.
		]
	},
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
			tags:{
				default: "Information",
				sql: "Information",
				exception: "Debug",
				parsing: "Trace",
				test: "Trace",
				sessions: "Trace",
				"http.client.write": "Debug",
				"http.client.read": "Debug",
				"http.server.write": "Debug",
				"http.server.read": "Debug",
				"socket.client.write": "Debug",
				"socket.client.read": "Debug",
				"socket.server.write": "Debug",
				"socket.server.read": "Debug",
				"socket.client.read.subscription": "Information",
				ql: "Debug",
				settings: "Debug"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		subscribe:{},
		proto:{//without this ProtoLog::Init is a no-op, there is no binary archive, and the `logs` query 500s on GetLogger<ProtoLog> - which is what the web ui's Logs tab reads.
			path: logsDir + "/app-server",
			timeZone: "America/New_York",
			delay: "PT1M",
			tags: {
				default: "Debug",
				externalLogger: "None"
			}
		},
		breakLevel: "Warning"
	},
	workers:{
		drive:{threads: 1},
		executor: {threads: 2},
	}
}
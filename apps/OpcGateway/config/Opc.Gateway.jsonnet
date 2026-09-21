local args = import 'args.libsonnet';
local logsDir = args.logsDir;
//The gateway's own OPC UA applicationUri:  what its clients tell a server they are, and so the uri SAN of every certificate
//they present - the per-connection issued ones (gateway.issuedCerts) and, under certificate authentication, the web
//certificate (http.ssl) - since a server holds the two against each other.  It is NOT a connection's certificateUri, which
//is the server's uri and only selects its endpoints (reviews/security-matrix.md #8).  A change re-issues on the same keys,
//and a third-party server then has the new certificate to trust.
local applicationUri = "urn:$(HostName):Jde-Cpp:$(PRODUCT_NAME)";
function( sync=false )
{
	local instance = self,
	gateway:{
		pingInterval: "PT30S",
		ttl: "PT2M",
		search:{ //the `search` query's per-connection node-name index (src/NodeIndex.cpp), crawled on the first search and dropped with the client at ttl.
			maxDepth: 12, //levels under Objects
			maxNodes: 25000, //stop crawling (results flagged truncated) beyond this
			browseBatch: 64, //folders browsed per request - the crawl browses a whole BFS level at once; a server refusing with BadTooManyOperations drops it to 1 for the rest of that crawl
			limit: 20, //default rows when the query passes no limit
			includeServer: false //index the ns=0 Server object's diagnostics subtree
		},
		//certificate.managed:false (default true) presents your own pair - one a CA the OPC servers trust issued - as found:  never
		//issued, never re-issued on expiry or a changed applicationUri.  One private key for the block (privateKey.path), and one
		//certificate per connection, looked for at <certificate.path, or the default>.<connection slug>.pem - a copy of the same
		//file will do;  a connection whose file is missing fails with that file's name.
		issuedCerts: {
			certificate:{
				subjectAltName: "URI:" + applicationUri,
				commonName: args.instanceName,
			},
			privateKey:{ passcode: "$(JDE_PASSCODE)" }
		},
		//Verify every OPC server's certificate against trustedCertDirs, below, before a session is opened (libs/opc ServerTrust.cpp).
		//open62541 checks the certificate of any endpoint that carries one - None security mode included - so a server whose
		//certificate is in none of those directories is refused BadCertificateUntrusted, and the connection error says which
		//server and what to do.  false accepts any certificate (the pre-2026-09 behaviour): a lab setting, never a deployment.
		verifyServerCertificate: true,
		//The OPC servers this gateway trusts, one certificate per server (.pem, .crt, .der or .cer - a UA server publishes DER),
		//read on every connect.  A Jde OpcServer on this host
		//publishes its own at certsDir("OpcServer"); any other server's certificate is copied into this product's own ssl/servers,
		//which the gateway creates at startup.  Not the OS root store - OPC server certificates are self-signed.  And not
		///access/trustedCertDirs, where this list lived until 2026-09-18:  in the hub that one is the AppServer role's enrollment
		//anchors - every certificate under it may create a user - and a server the gateway talks to has no business among them
		//(reviews/security-matrix.md #3).
		trustedCertDirs: [ args.certsDir("OpcServer"), args.serversDir("$(PRODUCT_NAME)") ],
		//A password or an issued token is sent encrypted wherever the server's token policy asks for that, certificateUri or not.
		//Where a server offers it only under a token policy of None on a channel that is not Sign & Encrypt, it would cross the
		//wire in the clear, and the gateway refuses the connection and says why.  true sends it anyway (open62541's
		//allowNonePolicyPassword): for a server that can do no better, on a network you trust - and the server has to accept
		//it too (reviews/security-matrix.md #6).
		allowPlaintextPassword: false
	},
	logging:{
		breakLevel: "Critical",
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				browse: "Trace",
				ql: "Trace",
				access: "Trace",
				"opc.access": "Trace",
				test: "Trace",
				externalLogger: "Information",
				"http.client.write": "Debug",
				"http.client.read": "Debug",
				"http.server.write": "Debug",
				"http.server.read": "Debug",
				"socket.client.write": "Debug",
				"socket.client.write.subscription": "Information",
				"socket.client.read": "Debug",
				"socket.client.read.subscription": "Information",
				"socket.server.write": "Debug",
				"socket.server.read": "Debug",
				settings: "Trace",
				uaEvent: "Debug",
				monitoring: "Information",
				processingLoop: "Information",
				opc: "Trace",
				uaNet: "Information",
				uaClient: "Information",
				uaSecure: "Information",
				uaSession: "Information",
				uaServer: "Information",
				uaUser: "Information",
				uaSecurity: "Information",
				uaPubSub: "Information",
				uaDiscovery: "Information"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		proto:{
			path: logsDir + "/opc-gateway",
			timeZone: "America/New_York",
			delay: "PT1M",
			tags: {
				default: "Debug",
				externalLogger: "None"
			}
		}
	},
	dbServers: {
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
	ql:{
		introspection: [
			"introspection/serverConnection.jsonnet",
			"introspection/search.jsonnet"
		]
	},
	credentials:{
		name: "OpcGateway",
		slug:: "OpcGateway"
	},
	web:{
		client:{ ssl:{ caFile: args.certsDir("AppServer")+"/AppServer.pem" } }//the AppServer is its own root - without an anchor the client rejects localhost:1967's self-signed cert.  The stem is App.Server.jsonnet's visible commonName; if that or its `path::` un-hides, this anchor must follow.
	},
	http:{
		address: null,
		host: "localhost",//advertised to the AppServer registry.  A loopback name is rewritten by the page to the host it was served from (web: resolveInstanceHost), which is what allowOrigin 'sameHost' needs; a real name is kept as the page's target.
		port: 1968,
		threads: 1,
		timeout:: "PT30M",
		socketTimeout:: "P1D",
		maxLogLength: 255,
		bodyLimit: 10000,
		accessControl: {
			allowOrigin: "sameHost",
			allowMethods: "GET, POST, OPTIONS",
			allowHeaders: "Content-Type, Authorization"
		},
		ssl: {
			certificate:{
				subjectAltName: "URI:" + applicationUri,
				company:: "Jde-Cpp",
				fileName: args.instanceName + ".web",
				commonName: args.instanceName + ".web.$(HostName)",
				country:: "US",
			},
			privateKey:{ passcode: "$(JDE_PASSCODE)" }, //the web key; same rule as gateway.issuedCerts.privateKey below.
			publicKey:: "{AppDataFolder}/ssl/public/public.pem",
			dh:: "{AppDataFolder}/certs/dh.pem"
		},
		clientSettings:{
			googleAuthClientId: "445012155442-1v8ntaa22konm0boge6hj5mfs15o9lvd.apps.googleusercontent.com"
		}
	},
	workers:{
		drive:{ threads:  2 },
		executor:{ threads:  3 }
	}
}
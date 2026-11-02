// The gateway's settings.  Loaded three ways: by the standalone Jde.Opc.Gateway (`-settings=<this file>
// -include=args/<dialect>`), by Opc.Hub.jsonnet, which imports it as a function and picks the keys the hub keeps, and
// by the soak harness's Opc.Gateway.Soak.jsonnet overlay.  `sync` is the `-sync` flag (schema sync on start);
// `args.libsonnet` is whichever config/args/<dialect> the `-include` names - there is no default.  An installed product
// reaches this file through the hub's config, whose args/install supplies the paths.  The keys someone would touch:
// /gateway (pingInterval, ttl, requestTimeout and subscription.publishingInterval for the OPC sessions,
// search's crawl caps, issuedCerts, verifyServerCertificate and trustedCertDirs for the servers it connects to,
// allowPlaintextPassword), /http address, host and port, /ql/introspection and /logging.
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
	gateway:{
		pingInterval: "PT30S",
		ttl: "PT2M",
		requestTimeout:: "PT10S", //a service call's deadline, and the test connect's, where the connection row sets no request_timeout (src/UAClient.cpp Create, UA_ClientConfig::timeout).
		subscription:: { //the one data subscription per client (src/async/Subscriptions.cpp CreateSubscriptionRequest), open62541's defaults.
			publishingInterval: "PT0.5S" //what the create asks for; the historian sizes its item queues and heartbeat hold from what the server revises it to.
		},
		search:{ //the `search` query's per-connection node-name index (src/NodeIndex.cpp), crawled on the first search and dropped with the client at ttl.
			maxDepth: 12, //levels under Objects
			maxNodes: 25000, //stop crawling (results flagged truncated) beyond this
			browseBatch: 64, //folders browsed per request - the crawl browses a whole BFS level at once; a server refusing with BadTooManyOperations drops it to 1 for the rest of that crawl
			limit: 20, //default rows when the query passes no limit
			includeServer: false, //index the ns=0 Server object's diagnostics subtree
			skipBrowseNames:: ["_Hints"] // Browse names neither indexed nor descended
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
		//read on every connect.  A Jde OpcServer on this host publishes its own at certsDir("OpcServer"), the PLC emulator's server
		//(apps/OpcServer/emulator, -transport=server) the device's pair at certsDir("PlcEmulator"); any other server's certificate is
		//copied into this product's own ssl/servers,
		//which the gateway creates at startup.  Not the OS root store - OPC server certificates are self-signed.  And not
		///access/trustedCertDirs, where this list lived until 2026-09-18:  in the hub that one is the AppServer role's enrollment
		//anchors - every certificate under it may create a user - and a server the gateway talks to has no business among them
		//(reviews/security-matrix.md #3).
		trustedCertDirs: [ args.certsDir("OpcServer"), args.certsDir("PlcEmulator"), args.serversDir("$(PRODUCT_NAME)") ],
		//The gateway's historian (libs/historian/docs/spec.md, *Placement*):  the groups defined on it.  No `hist` block, or another
		//process holding the lock under `path`, runs the gateway without its historian.  A `::` key is its default, shown hidden:
		//un-hide it to change it.
		hist:{
			path: logsDir + "/hist/opc-gateway", //where the group files and the lock go.  Set here, under this layout's logsDir:  the code's own fallback is <AppDataFolder>/logs/hist/opc-gateway.
			delay:: "PT1M", //ISO 8601:  how often each group's buffer is written, and how long after midnight a day's file becomes its archive.
			maxBuffer:: 67108864, //bytes, every group's buffer together (64 MB):  past it the oldest values are dropped with a gap marker.  At least 1048576.
			timeZone:: "UTC", //the zone the day directories follow, an IANA name.  It must not change once `path` holds files:  a read opens a day's file by it.
			readLimit:: 10000, //the most values one history read returns, a page;  a read's own `limit` can only lower it.  The pass-through reads take it too.
			describeBatch:: 64, //the members a group asks its server about at once, at most six operations each, for their types, ranges, attributes and paths.  A server that refuses a request as too many (BadTooManyOperations) halves it for the group.
			//What the collector logs in to each server with, by connection slug (*Collector contract*):  a user name and password for a
			//server that takes no certificate token, as in { "<slug>": { user: "collector", password: "$(COLLECTOR_PASSWORD)" } }.  A
			//connection with no entry uses the process's own certificate.
			credentials:: {}
		},
		//A password or an issued token is sent encrypted wherever the server's token policy asks for that, certificateUri or not.
		//Where a server offers it only under a token policy of None on a channel that is not Sign & Encrypt, it would cross the
		//wire in the clear, and the gateway refuses the connection and says why.  true sends it anyway (open62541's
		//allowNonePolicyPassword): for a server that can do no better, on a network you trust - and the server has to accept
		//it too (reviews/security-matrix.md #6).
		allowPlaintextPassword: false,
		//The gateway's own OPC UA endpoint (docs/servers/spec.md, src/server/GatewayServer.h):  what it collects, served to a SCADA, an
		//HMI or a test client as one address space, with the hub's sessions behind it.  No `server` block, no endpoint.  A `::` key is
		//its default, shown hidden.
		server:{
			port:: 4841, //the OpcServer has 4840 on the same dev box;  the tests take 4941
			address: if std.objectHas(args, 'listenAddress') then args.listenAddress else null, //null:  every interface;  args/install-user binds loopback, as the OpcServer does
			//absent:  the endpoint presents the process's own pair, /http/ssl, whose URI SAN already carries the gateway's applicationUri
			ssl:: {},
			trustedCertDirs:: [ args.serversDir("$(PRODUCT_NAME)") + "/clients" ], //the UA clients this endpoint admits on a secured channel and as certificate tokens:  not /access/trustedCertDirs, the hub's enrollment anchors
			rejectedCertDir:: args.serversDir("$(PRODUCT_NAME)") + "/rejected", //where a refused client certificate is written, <thumbprint>.der, for the admin to move into clients/
			none:: true, //a None endpoint beside the secured ones;  false keeps the None policy for discovery alone (securityPolicyNoneDiscoveryOnly):  GetEndpoints answers, a session needs a secured endpoint
			tokenTypes:: { anonymous: false, certificate: true, issued: true, username: true },
			//the user-name logins, as the OpcServer's /opc/users:  [ { name: "operator", password: "$(OPERATOR_PASSWORD)" } ].  Plain text at
			//rest, so a secret goes through the $(ENV) expansion as the key passcodes do;  the login's hub user is the row of that name.
			users:: [],
			userTokenPolicyUri:: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", //the policy the user tokens ride on;  "" names each security policy for itself
			writeTimeout:: "PT10S", //how long a client's Write of a served node holds for the source's answer (asyncOperationTimeout):  past it the client is answered Bad_Timeout and the source's answer, when it comes, is logged and dropped
			editTimeout:: "PT5S", //how long a HistoryUpdate holds for the flush that writes it, the OpcServer's hist.editTimeout:  past it the client is answered Bad_Timeout and the edit may still be written
			//open62541's own limits (0 unlimited where it reads so);  absent keeps them.  sessionTimeout and minSamplingInterval are ISO 8601;
			//maxSecureChannels is never left below maxSessions.
			limits:: { maxSessions: 100, maxSecureChannels: 100, maxMonitoredItems: 0, sessionTimeout: "PT1H", minSamplingInterval: "PT0.05S", maxAsyncOperations: 0 }
		}
	},
	logging:{
		breakLevel: "Critical",
		spd:{
			tags: {
				default: "Information",
				app: "Information",
				browse: "Information",
				ql: "Information",
				access: "Information",
				"opc.access": "Information",
				test: "Information",
				externalLogger: "Information",
				"http.client.write": "Information",
				"http.client.read": "Information",
				"http.server.write": "Information",
				"http.server.read": "Information",
				"socket.client.write": "Information",
				"socket.client.write.subscription": "Information",
				"socket.client.read": "Information",
				"socket.client.read.subscription": "Information",
				"socket.server.write": "Information",
				"socket.server.read": "Information",
				settings: "Information",
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
			//no timeZone:  the archive's day directories (<path>/<yyyy>/<m>/<d>) follow the machine's zone, ProtoLog's default - a developer's "America/New_York" here shipped to every install (reviews/m4-closing.md #14).  An IANA name pins them.
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
		localhost: args.dbServers.localhost
	},
	ql:{
		introspection: [
			"introspection/serverConnection.jsonnet",
			"introspection/search.jsonnet",
			"introspection/hist.jsonnet"
		]
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
			allowHeaders:: "Content-Type, Authorization, Content-Encoding"
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
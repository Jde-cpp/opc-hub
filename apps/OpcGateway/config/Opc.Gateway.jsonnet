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
		reconnectDelay:: "PT1S", //a lost connection's first wait before it reconnects, doubled after each failed attempt to at most 15 s (src/UAClient.cpp startReconnect).
		subscription:: { //the one data subscription per client (src/async/Subscriptions.cpp CreateSubscriptionRequest), open62541's defaults.
			publishingInterval: "PT0.5S", //what the create asks for; the historian sizes its item queues and heartbeat hold from what the server revises it to.
			lifetimeCount:: 10000, //publishing intervals the server keeps the subscription without a Publish from the gateway.
			maxKeepAliveCount:: 10, //publishing intervals without a notification before the server sends a keep-alive.
			maxNotificationsPerPublish:: 0, //per Publish response; 0 is unlimited.
			priority:: 0, //0-255, against the server's other subscriptions.
			itemDeleteDelay:: "PT1S" //how long an item outlives its last listener (src/types/MonitoringNodes.cpp DeleteMonitoring):  a node subscribed again within it joins the item, with no initial value.
		},
		search:{ //the `search` query's per-connection node-name index (src/NodeIndex.cpp), crawled on the first search and dropped with the client at ttl.
			maxDepth: 12, //levels under Objects
			maxNodes: 25000, //stop crawling (results flagged truncated) beyond this
			browseBatch: 64, //folders browsed per request - the crawl browses a whole BFS level at once; a server refusing with BadTooManyOperations drops it to 1 for the rest of that crawl
			limit: 20, //default rows when the query passes no limit
			includeServer: false, //index the ns=0 Server object's diagnostics subtree
			skipBrowseNames:: ["_Hints"] // Browse names neither indexed nor descended
		},
		browse:: { //a node page's browses (src/ql/NodeQLAwait.cpp)
			maxReferences: 1000 //the References tab's rows:  `truncated` past this - a type node's inverse HasTypeDefinition is every instance it has
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
		trustedCertDirs: [ args.certsDir("OpcServer"), args.certsDir("PlcEmulator"), self.serversDir ],
		//This product's own trusted-server directory, ssl/servers:  where an operator drops a third-party server's certificate, and
		//where Trust (trustServer) moves one - so it must stay among trustedCertDirs, above, for the move to mean anything.  A `::`
		//key is its default, shown hidden.
		serversDir:: args.serversDir("$(PRODUCT_NAME)"),
		//Where the verifier writes a server certificate it rejects, <thumbprint>.der (libs/opc ServerTrust.cpp), so the Connection
		//tab shows it and Trust can move it.  A `::` key is its default, shown hidden.  Not ssl/servers/rejected, the gateway's own
		//endpoint's /gateway/server/rejectedCertDir below, for the client certificates IT refuses (the other direction of trust),
		//which it prunes to the newest few.
		rejectedCertDir:: self.serversDir + "/rejected-servers",
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
		//OEE on the historian (docs/oee/spec.md, *Placement*):  the machines defined over its groups, each an engine that turns its
		//group's records into stops and interval rows.  No `oee` block, no OEE, and it needs `hist`, above:  without the historian
		//the gateway logs a Critical and runs without it.  A `::` key is its default, shown hidden.
		oee:{
			live:: "PT1S", //how often at most a client of OeeSubscribe hears a machine's figures;  a state change is pushed at once.
			settle:: "PT5S" //how long after an interval ends the engine writes its row, for records still on their way;  a member's MinTimeInterval past it makes each held value a rebuild.  Also how long after a machine's last edit its engine starts, so a form's signals land together.
		},
		//Flows (docs/flows/spec.md, *Placement*):  graphs of nodes the gateway runs, each deployed under its deployer's rights.  No
		//`flows` block, no flows.  A `::` key is its default, shown hidden.
		flows:{
			path: logsDir + "/flows/opc-gateway", //the trace's, a directory per flow by its slug, beside hist's:  a flow's purge removes its own.  Set here, under this layout's logsDir:  the code's own fallback is <AppDataFolder>/logs/flows/opc-gateway.
			queue:: 1000 //messages a node's input holds;  past it the oldest is dropped and counted.
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
		},
		//The drivers (docs/open-protocol/spec.md, *The driver server*):  a connection of a kind with one, an Open Protocol tightening
		//controller's, is served by a UA server of the gateway's own on loopback, which the row's clients dial in place of its url.
		//A `::` key is its default, shown hidden.
		drivers:{
			portBase:: 4870, //a driver row's server listens on 127.0.0.1 at portBase + its row id, every kind;  the tests take 4980, the hub's 5170
			//An Open Protocol row's link to its controller (src/drivers/openProtocol/OpenProtocolDriver.h).  The durations are ISO 8601.
			openProtocol:{
				keepAlive:: "PT10S", //MID 9999 after this long without a message either way;  a controller drops a link silent for 15 s
				replyTimeout:: "PT10S", //a connection, a request or an ack unanswered this long is a lost link
				callTimeout:: "PT30S", //how long a method call holds, the server's asyncOperationTimeout
				maxAsyncOperations:: 0, //the server's async queue, maxAsyncOperationQueueSize:  0 unlimited, as open62541's own
				reconnectMin:: "PT1S", //the wait between connection attempts, doubling to reconnectMax
				reconnectMax:: "PT1M",
				maxBackfill:: 2000, //MID 0064 requests one recovery may send
				maxSkew:: "PT5M", //the controller's clock against the gateway's, past which each connect warns
				log:: false //every frame in and out at Trace under the openProtocol tag
			}
		}
	},
	calendars:: { retryWait: "PT30S" }, //Calendars.cpp:  how soon a subscribe or read of the shift calendars that failed is tried again.
	logging:{
		breakLevel: "Critical",
		stack:: { default: "Warning" },//open62541's lines (LogStack):  which walk the stack for their file, line and function.
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
			file:: { default: "Warning", exception: "Trace" },//which entries keep their file and line in the log files, a logger's `tags` shape:  warnings, errors and exceptions.
			"function":: { default: "None", exception: "Trace" },//...and their function:  exceptions.
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
			"introspection/hist.jsonnet",
			"introspection/oee.jsonnet",
			"introspection/flows.jsonnet"
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
				//IP:127.0.0.1:  a driver row's server presents this pair on loopback, and its clients check the url's host against it.
				subjectAltName: "URI:" + applicationUri + ",DNS:localhost,DNS:$(HostName),IP:127.0.0.1",
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
		blocking:: { threads: 2 },//the connects' pool (src/async/BlockingPool.h):  a client's build and connect - the certificate issue, the trust scan, the host probe - run here, never on the executor.
		discovery:: { threads: 2 },//the discover mutation's pool (src/async/BlockingPool.h DiscoveryPool):  FindServers and GetEndpoints, up to 15 s each, never on the connects' pool.
		drive:{ threads:  2 },
		executor:{ threads:  3 }
	}
}
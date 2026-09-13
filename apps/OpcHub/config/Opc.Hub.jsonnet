// Jde.Opc.Hub - the AppServer and the OpcGateway in one process.  Composed from the two production configs so the
// role-specific blocks (gateway.*, the tag levels, the db mounts) keep tracking them; every top-level key is picked
// explicitly rather than `app + gw`, because jsonnet `+` replaces whole sub-objects (gw.access would drop the AppServer's
// trustedCertDirs, gw.logging its subscribe block).  Both imports resolve `args.libsonnet` through this file's
// -include dir (config/args/<dialect>), which mounts access + app + gateway together.
//
// One listener (/http, 1967) for both roles: the AppServer's REST and app-protocol socket at `/` (login, access, logs, the
// registry the OpcServer and the PLC emulator connect to) and the gateway's at `/opc` (the browser's OPC socket) - see
// src/WebServer.cpp.  One /graphql over access + app + gateway.  Launch: -settings=<this file> -include=args/mysql (or
// args/sqlite -arg path=…).
local args = import 'args.libsonnet';
local logsDir = args.logsDir;
function( sync=false )
	local app = (import '../../AppServer/config/App.Server.jsonnet')( sync );
	local gw = (import '../../OpcGateway/config/Opc.Gateway.jsonnet')( sync );
	//the web certificate's names beyond localhost, this machine ($(HostName), the settings expander's built-in) and the loopback
	//ip:  args.hostNames (args/install) adds the others a browser or a split OpcServer reaches the hub by, as DNS entries.
	local hostNames = if std.objectHas(args, 'hostNames') then std.join('', [',DNS:' + n for n in args.hostNames]) else '';
{
	instanceName: args.instanceName, //"OpcHub.<buildTarget>": the process's one connections{} row, the /opcGateways instanceName, and the cert CN root.
	gateway: gw.gateway, //pingInterval/ttl/search/issuedCerts/verifyServerCertificate - the issued OPC client certs land under $(ProgramData)/Jde-Cpp/OpcHub via ProductName.
	//One list serves both roles: the AppServer's enrollment anchors (Access::Server::Trust - the OpcServer and the PLC
	//emulator log in with their client certs) and the gateway's OPC server trust (src/ServerTrust.cpp).  No OpcGateway
	//entry: no separate gateway process logs in to this one.
	access:{
		trustedCertDirs: [ args.certsDir("OpcServer"), args.certsDir("PlcEmulator") ]
	},
	dbServers: app.dbServers, //args-driven: the hub args mount access + app + gateway and list all three script dirs.
	logging:{
		breakLevel: "Critical", //the gateway's: a hub that BREAKs on Warning is unusable under a debugger.
		spd: app.logging.spd + { tags: app.logging.spd.tags + gw.logging.spd.tags },
		subscribe: {}, //the AppServer's SubscribeLog - the Logs tab's live feed.
		proto: gw.logging.proto + { path: logsDir + "/opc-hub" }, //one binary archive for the process; the `logs` query reads it.
	},
	ql:{
		introspection: [ "../../OpcGateway/config/" + p for p in gw.ql.introspection ] //Settings::Directory()-relative (gatewayStartup.cpp) - the gateway's extension types.
	},
	credentials: gw.credentials, //the gateway's user name "OpcGateway" - the seeded role/group (config/access-opcGateway.mutation) still applies.
	//no `web.client.ssl.caFile`: the gateway anchors the AppServer's cert for its login - there is none here.
	http:{
		address: null,
		host: "localhost", //advertised through /opcGateways.  A loopback name here reaches the hub only from its own machine, so the page rewrites it to the host it was served from (web: resolveInstanceHost) - which is also what allowOrigin 'sameHost' needs.  Set a real name only for a split deployment the page must reach elsewhere.
		port: 1967, //the AppServer's port: web/opc/site/environments/environment*.ts (applicationServer) stays as it is.
		threads: app.http.threads,
		timeout: app.http.timeout,
		socketTimeout:: app.http.socketTimeout,
		maxLogLength: app.http.maxLogLength,
		bodyLimit: app.http.bodyLimit,
		accessControl: app.http.accessControl,
		//the Web UI's Google login: an args file may name the site's own OAuth client id (args/install does - setup/README.md "First login"); otherwise the AppServer's.
		clientSettings: app.http.clientSettings + ( if std.objectHas(args, 'googleAuthClientId') then { googleAuthClientId: args.googleAuthClientId } else {} ),
		ssl:{
			//args/install may supply the operator's own pair in place of the product's self-signed one:  certificate:{ managed:false,
			//path:… }, privateKey:{ path:…, passcode:… } - used as found, never issued or replaced (setup/README.md "Notes").
			certificate:{
				//DNS/IP: what TLS clients (the OpcServer's login) match - localhost, this machine's own name (DNS:$(HostName), so
				//https://<machine>:1967 passes the name check from another browser once the certificate is trusted there) and the
				//loopback ip, plus args.hostNames.  A change re-issues on the same key at the next start (Crypto::ReissueReason).
				//URI: the gateway role authenticates to OPC servers with this cert too (UAClient certificate authentication signs
				//with the app client's SslSettings), and open62541 wants the application uri in the SAN - as the gateway's own web
				//cert carries it.
				subjectAltName: "URI:urn:open62541.server.application,DNS:localhost,DNS:$(HostName),IP:127.0.0.1" + hostNames,
				country: "US",
				commonName: "OpcHub" //-> $(ProgramData)/Jde-Cpp/OpcHub/ssl/certs/OpcHub.pem: what the OpcServer/PlcEmulator hub overlays anchor (web.client.ssl.caFile).
			} + (if std.objectHas(args, 'certificate') then args.certificate else {}),
			privateKey:{ passcode: "$(JDE_PASSCODE)" } + (if std.objectHas(args, 'privateKey') then args.privateKey else {})
		}
	},
	workers:{
		drive:{ threads: 2 },
		executor:{ threads: 4 }
	}
}

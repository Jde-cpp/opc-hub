// Soak-client config. No database - the client only speaks to the AppServer (:1967) and the gateway's WebSocket (:1968).
// Durations/paths under `soak` are defaults; soak.sh overrides per-run via CLI flags (-duration=… beats the setting).
// `soak.servers` lists the gateway connections to drive; an entry with `flag` only runs when that CLI arg is present
// (-external here - soak.sh --external), so Linux/CI runs are unchanged. For the flagged entry, -externalUrl=/
// -externalUri=/-externalUser=/-externalPwd= override url/certificateUri/user/password (never commit a real password).
local logsDir = std.extVar("logsDir");
local gatewayProduct = "OpcGateway"; //must match the gateway process's ProductName - that is whose cert tree -createCert writes into.
{
	//-createCert pre-creates the gateway's per-leg client certs through UAClient's own helpers, so this block has to
	//resolve to the same files the gateway process would: its product (not the soak exe's "Opc.Soak") and the CN
	//Opc.Gateway.jsonnet configures, `args.instanceName` = <product>.<buildTarget>.  The passcode likewise matches - both
	//sides read $(JDE_PASSCODE), so a key -createCert writes here is one the gateway can open, encrypted or not.
	//getPath reads productName from each key's own sub-object; a block-level one is now inherited by all three
	//(withDefaultProductName), so these lines are redundant rather than required - kept explicit because the point of
	//the block is to write into another process's cert tree, which is worth stating on each key it moves.
	gateway:{
		issuedCerts:{
			certificate:{
				productName: gatewayProduct,
				commonName: gatewayProduct+"."+std.extVar("buildTarget"),
				subjectAltName: "URI:urn:$(HostName):Jde-Cpp:"+gatewayProduct //the gateway's own applicationUri, spelled as Opc.Gateway.jsonnet's resolves in the gateway process - $(PRODUCT_NAME) here would be the soak exe's.  A different SAN and the gateway re-issues at its first connect, voiding the trust set up before the run (security-matrix #8).
			},
			privateKey:{ productName: gatewayProduct, passcode: "$(JDE_PASSCODE)" },
			publicKey:{ productName: gatewayProduct }
		}
	},
	server:{
		host: "localhost",
		port: 1967,
		isSsl: false
	},
	credentials:{
		name: "OpcSoak"
	},
	web:{
		//the AppServer is its own root - without this anchor the client rejects the self-signed cert localhost:1967 presents on the /login call
		//(the same entry Opc.Gateway.jsonnet carries; no args.libsonnet here, so the path is spelled out the way Opc.PlcEmulator.jsonnet does).
		client:{ ssl:{ caFile: "$(ProgramData)/Jde-Cpp/AppServer/ssl/certs/AppServer.pem" } }
	},
	soak:{
		gatewayHost: "localhost",
		gatewayPort: 1968,
		duration: "PT24H",
		writePeriod: "PT1S",
		pushTimeout: "PT5S",
		writeRetries: 2, //extra attempts a failed updateVariable gets (same value) before it counts as a writeFailure; retries are reported separately.
		missRetries: 2, //extra rounds a cycle gets when the write was acked but no push arrived within pushTimeout - each re-sends a fresh value; reported separately too.
		retryDelay: "PT1S",
		statusPeriod: "PT1M",
		quietInterval: "PT6H",
		quietPeriod: "PT10M",
		servers: [
			{
				slug: "OpcSoak", name: "Soak test server", description: "Soak test connection",
				certificateUri: "urn:open62541.server.application",
				url: "opc.tcp://127.0.0.1:4840",
				nodes: [ { ns: 4, id: 6017 } ] //writable numeric var from IA examples nodeset - same node SubscribeTests uses.
			},
			{
				flag: "-external", //leg active only when this CLI arg is present.
				slug: "ExternalSoak", name: "External soak server",
				description: "Externally-managed OPC-UA server (not launched/monitored by soak.sh)",
				certificateUri: "", //set via -externalUri= to the server's application URI (raw; %20-encoded at use) - required for a secured session; empty falls back to SecurityPolicy None - no client cert on the channel, the login still encrypted.
				url: "opc.tcp://127.0.0.1:49320",
				user: "soak", //server account with tag-write access - presence of `user` makes the leg log in.
				password: "", //set via -externalPwd=.
				nodes: [ { ns: 2, s: "Data Type Examples.16 Bit Device.K Registers.Long1" } ] //writable static Int32 tag - an Int16 tag overflows the PT1S counter at ~9.1h.
			}
		]
	},
	http:{
		ssl:{
			productName: "Opc.Soak" //keys under $(companyDir)/Opc.Soak/ssl on every OS - windows has no .rc ProductName for this exe.
		}
	},
	logging:{
		spd:{
			tags:{
				default: "Information",
				uaNet: "Warning",
				uaClient: "Warning",
				uaSecure: "Warning",
				uaSession: "Warning"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		}
	},
	workers:{
		drive:{ threads: 1 },
		executor:{ threads: 2 }
	}
}

//Jde.Opc.PlcEmulator.Tests - the emulator's units:  Signals, ParseDevices, PlcServer.  No database, no AppServer, no
//OpcServer, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert (there is no args/ dir to import).
//REPO_SOURCE_DIR is in ctest's environment (addJdeTest) and in the shell for direct runs (see the cpp-tests skill).
local logsDir = std.extVar("logsDir");
local applicationUri = "urn:jde:plc-emulator-tests";
{
	testing:{
		tests:: "*",
		//The suite's UA client:  a pair in this product's own dir, the one /emulator/plc/trustedCertDirs names, and a stranger's in a
		//dir it does not, which the PLC server refuses.
		client:{ ssl:{ productName: "Tests.Opc.PlcEmulator", certificate:{ commonName: "PlcEmulatorTests.client", subjectAltName: "URI:"+applicationUri+":client" } } },
		stranger:{ ssl:{ productName: "Tests.Opc.PlcEmulator.Stranger", certificate:{ commonName: "PlcEmulatorTests.stranger", subjectAltName: "URI:"+applicationUri+":stranger" } } }
	},
	instanceName: "PlcEmulatorTests",
	emulator:{
		//The contract the emulator publishes, on unicast test ports:  the PLC server on 4851 (PubSubTests' Publisher holds
		//4850, the real emulator 4842), the writer to 4852/udp - nothing listens there, and a writer only sends.
		pubsub: (import '../../../config/pubsub/pumps.libsonnet') + { url: "opc.udp://127.0.0.1:4852/" },
		//the device's pair, what plc.ssl falls back to (PlcServerTests.DefaultsWithoutTheBlock).
		ssl:{ productName: "Tests.Opc.PlcEmulator", certificate:{ commonName: "PlcEmulatorTests.opc", subjectAltName: "URI:"+applicationUri } },
		//The PLC server on the library's endpoint (servers plan Phase 6):  its own pair, a trust list of this product's dir, a username
		//login - there is no hub here, so it resolves to no user and an issued token is refused - and the limits the block reads.
		plc:{
			port: 4851,
			address: "127.0.0.1",
			nodeset: "$(REPO_SOURCE_DIR)/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml",
			ssl:{ productName: "Tests.Opc.PlcEmulator", certificate:{ commonName: "PlcEmulatorTests.plc", subjectAltName: "URI:"+applicationUri } },
			trustedCertDirs: [ "$(ProgramData)/Jde-Cpp/$(PRODUCT_NAME)/ssl/certs" ],
			users: [ { name: "plc", password: "plc" } ],
			limits: { maxSessions: 20, maxSecureChannels: 25 }
		}
	},
	logging:{
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				test: "Trace",
				settings: "Debug",
				opc: "Debug"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		}
	},
	workers:{
		executor: {threads: 2}
	}
}
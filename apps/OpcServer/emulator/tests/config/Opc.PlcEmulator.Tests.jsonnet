//Jde.Opc.PlcEmulator.Tests - the emulator's units:  Signals, ParseDevices, PlcServer.  No database, no AppServer, no
//OpcServer, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert (there is no args/ dir to import).
//REPO_SOURCE_DIR is in ctest's environment (addJdeTest) and in the shell for direct runs (see the cpp-tests skill).
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "PlcEmulatorTests",
	emulator:{
		//The contract the emulator publishes, on unicast test ports:  the PLC server on 4851 (PubSubTests' Publisher holds
		//4850, the real emulator 4842), the writer to 4852/udp - nothing listens there, and a writer only sends.
		pubsub: (import '../../../config/pubsub/pumps.libsonnet') + { url: "opc.udp://127.0.0.1:4852/" },
		plc:{
			port: 4851,
			bind: "127.0.0.1",
			nodeset: "$(REPO_SOURCE_DIR)/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml"
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
//Server-free Jde.Opc coverage - the uatypes wrappers and their round trips.  No dbServers block: nothing here opens a
//data source, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert (there is no args/ dir to import).
//The opc/browse tag names resolve through Opc::UALogParser, which main.cpp registers before Process::Startup.
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "OpcLibTests",
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

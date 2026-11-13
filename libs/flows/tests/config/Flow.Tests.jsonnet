//No dbServers block:  nothing here opens a data source, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is
//inert.  Two executor threads, so a flow's strand moves between them.
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "FlowTests",
	logging:{
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				test: "Trace",
				settings: "Debug"
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
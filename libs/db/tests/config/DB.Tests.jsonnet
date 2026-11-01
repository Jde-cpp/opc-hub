//Driver-agnostic Jde.DB coverage - generators, Value, DBException.  No dbServers block: nothing here opens a data
//source, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert (there is no args/ dir to import).
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "DBTests",
	logging:{
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				test: "Trace",
				settings: "Debug",
				sql: "Debug"
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
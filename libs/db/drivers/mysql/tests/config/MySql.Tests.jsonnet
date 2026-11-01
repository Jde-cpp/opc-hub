//Dialect-only coverage for the mysql driver: MySqlSyntax is header-only, so nothing here connects to a server.
//No dbServers block, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert (there is no args/ dir).
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "MySqlTests",
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
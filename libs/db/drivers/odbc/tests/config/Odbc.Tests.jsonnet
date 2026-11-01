//Connect-failure coverage for the odbc driver (install-issues #66): a DSN that does not exist fails where an
//unreachable server does, so no SQL Server is needed.  The SQL Server dialect is the base DB::Syntax, pinned in
//Jde.DB.Tests.  No dbServers block, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert (no args/ dir).
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "OdbcTests",
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
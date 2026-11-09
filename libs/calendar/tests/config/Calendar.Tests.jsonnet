//Jde.Calendar.Tests:  the resolver and the rule checks over tables of instants.  No dbServers block and no args/ dir, so
//addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert;  `-tests`/`-ctest` binds logsDir.
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "CalendarTests",
	calendars:{ retryWait: "PT0.2S" }, //CalendarsClientTests waits on it.
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
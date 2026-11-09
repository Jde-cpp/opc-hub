//Jde.Opc.Oee.Tests:  the engine, the formulas and the rules.  No dbServers block and no args/ dir, so addJdeTest's
//`-include=args/sqlite -arg path=:memory:` is inert;  `-tests`/`-ctest` binds logsDir.
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "OeeTests",
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
//Jde.Fwk.Tests: the framework - chrono, strings, json, settings, files, io drive, cache, timers, process, exceptions.
//No data source and no args/ dir, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert; `-tests`/`-ctest`
//binds logsDir, which is where testing.file and the logs land.  cryptoTests.clear keeps the generated key material;
//workers.blockStallWarning is short so the stall warning is observable.
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "ChronoTests.ToTimePointRejectsGarbage",
		file: logsDir + "/tests/test.txt"
	},
	cryptoTests:{
		clear: false
	},
	logging:{
		breakLevel: "Critical",
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				io: "Information",
				test: "Trace",
				settings: "Trace"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		memory:{
			tags: {
				default: "Debug"
			}
		}
	},
	workers:{
		blockStallWarning: "PT0.2S",//short enough for BlockAwaitTests.WarnsWhileStalled to observe; production defaults to 30s.
		executor: {threads: 2},
		io: {chunkByteSize: 10, threads: 2}
	}
}
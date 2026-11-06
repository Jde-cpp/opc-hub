//Data-source-free Jde.App.Shared coverage - the archive filter/sort/json, the string cache and the proto builders.
//No dbServers block: nothing here opens a data source, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is
//inert (there is no args/ dir to import).
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*"
	},
	instanceName: "AppTests",
	logging:{
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				test: "Trace",
				settings: "Debug",
				externalLogger: "Debug"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		//The `binary` logger of the logSetting query/mutation.  T9: `delay` is a minute so no timer round fires of its own
		//accord, but the size does get reached - LogTests writes filler until it crosses ProtoLog's 8096-byte _delaySize,
		//because the flushes and the archive rounds that follow them are what those tests are about.  Nothing here may
		//assume a sub-second run: the archive tree the rounds leave behind grows with every run into the same directory,
		//which is what WaitForFile's comment in LogTests is about.
		proto:{
			path: logsDir + "/app-tests",
			timeZone: "America/New_York", //explicit: LogTests asserts the <root>/2025/1/<day> archive folders, which are local dates.
			delay: "PT1M",
			file: { default: "Trace" },//every entry keeps its source:  LogProtoTests, ArchiveQueryTests and LogTests assert files, functions and lines.
			"function": { default: "Trace" },
			tags: {
				default: "Information",
				externalLogger: "None"
			}
		}
	},
	http:{
		port: 5010 //FromClient::Instance stamps this into the instance message; never bound - nothing here listens.
	},
	workers:{
		executor: {threads: 2}
	}
}
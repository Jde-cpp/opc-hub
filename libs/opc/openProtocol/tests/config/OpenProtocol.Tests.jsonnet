//Jde.Opc.OpenProtocol.Tests:  the codec over byte buffers, and the session against the controller's side of it on
//the executor's threads and loopback ports the system picks.  No dbServers block and no args/ dir, so addJdeTest's
//`-include=args/sqlite -arg path=:memory:` is inert;  `-tests`/`-ctest` binds logsDir.  The openProtocol tag's name resolves
//through OpenProtocol::TagNames, which main.cpp registers before Process::Startup.
//REPO_SOURCE_DIR is in ctest's environment (addJdeTest) and in the shell for direct runs (see the cpp-tests skill).
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "*",
		corpus: "$(REPO_SOURCE_DIR)/libs/opc/openProtocol/tests/corpus" //the fuzz target's, which CorpusTests replays.
	},
	instanceName: "OpenProtocolTests",
	logging:{
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				test: "Trace",
				settings: "Debug",
				openProtocol: "Information" //the corpus's frames log bytes past their layouts at Debug, hundreds of lines.
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
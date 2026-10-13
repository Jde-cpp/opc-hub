//Jde.QL.Tests:  the parser, Input arg/variable handling and the in-memory filters, which open nothing - and, since #238, the
//stock ops, ToWhereClause and the subscription fan-out against a real data source:  the `qlTests` schema (qlTests-meta.jsonnet)
//on sqlite.  Db-backed, so `-include=args/<dialect>` is required (args/ here, sqlite only):  under ctest addJdeTest passes
//args/sqlite with path=:memory:, so no server is needed and nothing is written;  a direct run passes the same two flags.
local args = import 'args.libsonnet';
local logsDir = args.logsDir;
{
	testing:{
		tests:: "*"
	},
	instanceName: "QLTests",
	dbServers:{
		localhost: args.dbServers.localhost
	},
	logging:{
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				test: "Trace",
				settings: "Debug",
				ql: "Debug",
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
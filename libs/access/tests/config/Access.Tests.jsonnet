//Jde.Access.Tests: the access library against a real data source - acl, authorize, roles, groups, users, login,
//subscriptions, profiles, the hooks and the views.  Db-backed, so `-include=args/<dialect>` is required (args/ here):
//under ctest addJdeTest passes args/sqlite with path=:memory:, so no server is needed; args/mysql and args/sqlServer
//run the same suites against a live server.  dataPaths is this library's config, so access.mutation seeds the rights
//and provider types on the fresh schema each run.
local args = import 'args.libsonnet';
local logsDir = args.logsDir;
{
	testing:{
		tests:: "RoleTests.Crud",
		recreateDB: std.parseJson( std.extVar("recreateDB") )
	},
	dbServers:{
		scriptPaths: [args.repoSourceDir+"/libs/access/config/sql/"+args.sqlType],
		dataPaths: [args.repoSourceDir+"/libs/access/config"],
		sync:: true,
		localhost:{
			driver: args.dbServers.localhost.driver,
			connectionString: args.dbServers.localhost.connectionString,
			username: args.dbServers.localhost.username,
			password: args.dbServers.localhost.password,
			schema: args.dbServers.localhost.schema,
			catalogs: args.dbServers.localhost.catalogs
		}
	},
	logging:{
		spd:{
			tags: {
				default: "Information",
				test: "Trace",
				access: "Trace",
				app: "Trace",
				settings: "Debug",
				ql: "Debug",
				sql: "Debug",
				exception: "Trace"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		memory:{ //kept past Init only with a level under `tags` - LoginTests reads it (Logging::Find).
			tags: { default: "Debug" }
		}
	},
	workers:{
		executor: {threads: 2}
	}
}
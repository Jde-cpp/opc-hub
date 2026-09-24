// The SQL Server dev args, Windows only (the odbc driver builds nowhere else): `-include=args/sqlServer`.  args-common
// binds the ext vars and the roots; the connection is DSN=<debug|rls> by build target with the odbc driver from the
// build's bin, the tables under dbo with their `access_`/`app_` prefixes.
local common = import '../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	sqlType: "sqlServer",
	dbServers: {
		dataPaths: [args.repoSourceDir + "/apps/AppServer/config", args.repoSourceDir + "/libs/access/config"],
		scriptPaths:  [args.repoSourceDir + "/apps/AppServer/config/sql/sqlServer", args.repoSourceDir + "/libs/access/config/sql/sqlServer"],
		localhost:{
			driver: args.repoBuildDir+"/bin/Jde.DB.Odbc.dll",
			connectionString: "DSN="+args.schema(),
			username: null,
			password: null,
			schema: null,
			catalogs: {
				[args.schema()]: {
					schemas:{
						dbo:{
							access:{  //test debug with schema, debug with default schema ie dbo.
								meta: args.repoSourceDir + "/libs/access/config/access-meta.jsonnet",
								ql: args.repoSourceDir + "/libs/access/config/access-ql.jsonnet",
								prefix: "access_"  //test with null prefix, debug with prefix
							},
							app:{
								meta: args.repoSourceDir + "/apps/AppServer/config/app-meta.jsonnet",
								prefix: "app_"
							}
						}
					}
				}
			}
		}
	}
}
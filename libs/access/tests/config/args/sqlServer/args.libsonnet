//The SQL Server args for Jde.Access.Tests, Windows only (the odbc driver builds nowhere else):
//`-include=args/sqlServer`, a System DSN `TestAccessDebug` onto database test_access_debug, the access tables in schema
//`acc` with no prefix, the odbc driver from the build's bin.  args-common binds the ext vars and the roots.
local common = import '../../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	sqlType: "sqlServer",
	dbServers: {
		localhost:{
			driver: args.repoBuildDir+"/bin/Jde.DB.Odbc.dll",
			connectionString: "DSN=TestAccessDebug",
			username: null,
			password: null,
			schema: null,
			catalogs: {
				test_access_debug: {
					schemas:{
						acc:{   //test debug with schema, debug with default schema ie dbo.
							access:{
								meta: "$(JDE_DIR)/libs/access/config/access-meta.jsonnet",
								ql: "$(JDE_DIR)/libs/access/config/access-ql.jsonnet",
								prefix: null  //test with null prefix, debug with prefix
							}
						}
					}
				}
			}
		}
	}
}
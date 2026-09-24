// The SQL Server dev args, Windows only (the odbc driver builds nowhere else): `-include=args/sqlServer`.  args-common
// binds the ext vars and the roots; the connection is DSN=debug with the odbc driver from the build's bin, the gateway
// tables under dbo with the `opc` prefix, and the access meta mounted for its types only (`_access`, no twins).
local common = import '../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	sqlType: "sqlServer",
	dbServers: {
		scriptPaths: [args.repoSourceDir+"/apps/OpcGateway/config/sql/sqlServer"],
		localhost:{
			driver: args.repoBuildDir+"/bin/Jde.DB.Odbc.dll",
			connectionString: "DSN=debug",
			username: null,
			password: null,
			schema: null,
			catalogs: {
				[args.schema()]: {
					schemas:{
						_access:{
							access:{
								meta: args.repoSourceDir+"/libs/access/config/access-meta.jsonnet"
							}
						},
						dbo:{
							gateway:{
								meta: args.repoSourceDir+"/apps/OpcGateway/config/opcGateway-meta.jsonnet",
								prefix: "opc"  //test with null prefix, debug with prefix
							}
						}
					}
				}
			}
		}
	}
}
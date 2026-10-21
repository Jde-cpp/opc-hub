// The SQL Server dev args, Windows only (the odbc driver builds nowhere else): `-include=args/sqlServer`.  args-common
// binds the ext vars and the roots; the connection is DSN=<debug|rls> by build target with the odbc driver from the
// build's bin, the `opc` schema under dbo with the `opc_` prefix, and the access schema mounted under `_appServer`
// (meta and ql, no prefix).  No scripts: the opc meta declares only the nodeIds resource.
local common = import '../../../../../libs/db/config/args-common.libsonnet';
local server = import '../opcServer-common.libsonnet';
common + {
	local args = self,
	sqlType: "sqlServer",
	instanceName: server.instanceName( args ),
	access: { trustedCertDirs: server.trustedCertDirs( args ) },
	dbServers: {
		dataPaths: [],
		scriptPaths: [],
		localhost:{
			driver: args.repoBuildDir+"/bin/Jde.DB.Odbc.dll",
			connectionString: "DSN="+args.schema(),
			username: null,
			password: null,
			schema: null,
			catalogs: {
				[args.schema()]: {
					schemas: server.mounts( args, "dbo" )
				}
			}
		}
	}
}
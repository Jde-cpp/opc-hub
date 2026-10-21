// The MySQL dev args: `-include=args/mysql`, a MySQL on localhost.  args-common binds the ext vars and the build/source
// roots; the login is $(JDE_MYSQL_USER)/$(JDE_MYSQL_PWD), the `opc` schema sits in `debug` or `rls` by build target
// (args.schema()) with the `opc_` prefix, and the access schema is mounted under `_appServer` (meta and ql, `access_`
// prefix; the `_` prefix means no twins here).  No scripts: the opc meta declares only the nodeIds resource.
local common = import '../../../../../libs/db/config/args-common.libsonnet';
local server = import '../opcServer-common.libsonnet';
common + {
	local args = self,
	sqlType: "mysql",
	instanceName: server.instanceName( args ),
	access: { trustedCertDirs: server.trustedCertDirs( args ) },
	dbServers: {
		dataPaths: [],
		scriptPaths: [],
		localhost:{
			driver: args.repoBuildDir + "/libs/db/drivers/mysql/lib/libJde.DB.MySql.so",
			connectionString: null,
			username: "$(JDE_MYSQL_USER)",
			password: "$(JDE_MYSQL_PWD)",
			schema: args.schema(),
			catalogs: {
				master: { // n/a for mysql
					schemas: server.mounts( args, args.schema(), "access_" )
				}
			}
		}
	}
}
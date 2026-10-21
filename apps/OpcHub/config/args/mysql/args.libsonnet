local common = import '../../../../../libs/db/config/args-common.libsonnet';
local hub = import '../hub-common.libsonnet';
//The MySQL dev args: `-include=args/mysql`, a MySQL on localhost, the login $(JDE_MYSQL_USER)/$(JDE_MYSQL_PWD);
//args-common binds the ext vars and the build/source roots.  The AppServer's mounts plus the gateway's, in the one
//<buildTarget> schema - the same table names (access_*, app_*, opc_*) the split AppServer + gateway create, so the hub
//runs against the existing data.
common + {
	local args = self,
	sqlType: "mysql",
	dbServers: {
		dataPaths: hub.dataPaths( args ),
		scriptPaths: hub.scriptPaths( args ),
		localhost:{
			driver: args.repoBuildDir + "/libs/db/drivers/mysql/lib/libJde.DB.MySql.so",
			connectionString: null,
			username: "$(JDE_MYSQL_USER)",
			password: "$(JDE_MYSQL_PWD)",
			schema: args.schema(),
			catalogs: {
				master: { // n/a for mysql
					schemas:{ [args.schema()]: hub.mounts( args ) }
				}
			}
		}
	}
}
// The MySQL dev args: `-include=args/mysql`, a MySQL on localhost.  args-common binds the ext vars and the build/source
// roots; the login is $(JDE_MYSQL_USER)/$(JDE_MYSQL_PWD), the schema `debug` or `rls` by build target (args.schema()),
// and the access and app tables live in it with their `access_`/`app_` prefixes.
local common = import '../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	sqlType: "mysql",
	dbServers: {
		dataPaths: [ args.repoSourceDir + "/apps/AppServer/config", args.repoSourceDir + "/libs/access/config"],
		scriptPaths:  [args.repoSourceDir + "/apps/AppServer/config/sql/mysql", args.repoSourceDir + "/libs/access/config/sql/mysql"],
		localhost:{
			driver: args.repoBuildDir + "/libs/db/drivers/mysql/lib/libJde.DB.MySql.so",
			connectionString: null,
			username: "$(JDE_MYSQL_USER)",
			password: "$(JDE_MYSQL_PWD)",
			schema: args.schema(),
			catalogs: {
				master: { // n/a for mysql
					schemas:{
						[args.schema()]:{
							access:{  //test debug with schema, debug with default schema ie dbo.
								meta: args.repoSourceDir + "/libs/access/config/access-meta.jsonnet",
								ql: args.repoSourceDir + "/libs/access/config/access-ql.jsonnet",
								prefix: "access_"  //test with null prefix, debug with prefix
							},
							app:{
								meta: args.repoSourceDir + "/apps/AppServer/config/app-meta.jsonnet",
								prefix: "app_"  //test with null prefix, debug with prefix
							},
						}
					}
				}
			}
		}
	}
}
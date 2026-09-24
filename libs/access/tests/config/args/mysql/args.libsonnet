//The MySQL args for Jde.Access.Tests: `-include=args/mysql`, a MySQL on localhost, the login
//$(JDE_MYSQL_USER)/$(JDE_MYSQL_PWD), the schema `<buildTarget>_access` with no table prefix.  Its own preamble rather
//than args-common: it predates it, and binds the same buildTarget/logsDir/repo roots itself.
{
  local args = self,
	local buildTarget = std.extVar("buildTarget"),
	local repoBuildDir = "$(REPO_BUILD_DIR)/"+buildTarget,
	repoSourceDir: "$(REPO_SOURCE_DIR)",
	sqlType: "mysql",
	logsDir: std.extVar("logsDir"),
	dbServers: {
		localhost:{
			driver: repoBuildDir+"/libs/db/drivers/mysql/lib/libJde.DB.MySql.so",
			connectionString: null,
			username: "$(JDE_MYSQL_USER)",
			password: "$(JDE_MYSQL_PWD)",
			schema: buildTarget+"_access",
			catalogs: {
				master: {  // N/A for mysql
					schemas:{
						[buildTarget+"_access"]:{//test debug with schema, debug with default schema ie dbo.
							access:{
								meta: args.repoSourceDir+"/libs/access/config/access-meta.jsonnet",
								ql: args.repoSourceDir+"/libs/access/config/access-ql.jsonnet",
								prefix: null  //test with null prefix, debug with prefix
							},
						}
					}
				}
			}
		}
	},
}
//The MySQL variant of the installed layout.  Like args/install-sqlServer it is reached by hand - `-include=args/install-mysql`
//on the service's command line, a database `jde` on the server, and its login in the service's environment as JDE_MYSQL_USER
//and JDE_MYSQL_PWD (/etc/jde-cpp/env on linux, system environment variables on windows); the installer and the package put the
//driver beside the exe - see setup/README.md and setup/linux/README.md.  args/install and only the database overridden, so
//the logs, certificates, Web UI, OAuth client id and host names are that install's.
(import '../install/args.libsonnet') + {
	local args = self,
	local hubDir = args.companyDir+"/OpcHub",
	sqlType: "mysql",
	dbServers: { //replaces args/install's whole block:  nothing of sqlite's catalogs carries over
		dataPaths: [hubDir+"/sql"], //the installer's seeds, dialect-free
		scriptPaths: [hubDir+"/sql-mysql"], //the MySQL procs, copied by hand:  never sql/, which every reinstall or upgrade refills with the sqlite scripts (reviews/m4-closing.md #18, #19)
		localhost:{
			driver: "$(ExeDir)/$(LibPrefix)Jde.DB.MySql$(LibExt)", //Jde.DB.MySql.dll, libJde.DB.MySql.so - beside the exe
			host: "localhost",
			port: 3306,
			connectionString: null,
			username: "$(JDE_MYSQL_USER)",
			password: "$(JDE_MYSQL_PWD)",
			schema: "jde", //the database
			catalogs: {
				master: { // n/a for mysql
					schemas:{
						jde:{
							access:{ meta: hubDir+"/access-meta.jsonnet", ql: hubDir+"/access-ql.jsonnet", prefix: "access_" },
							app:{ meta: hubDir+"/app-meta.jsonnet", prefix: "app_" },
							gateway:{ meta: hubDir+"/opcGateway-meta.jsonnet", prefix: "gateway_" }
						}
					}
				}
			}
		}
	}
}

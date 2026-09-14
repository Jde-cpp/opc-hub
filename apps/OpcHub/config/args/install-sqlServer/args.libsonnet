local paths = import '../../../../../libs/db/config/paths-common.libsonnet';
//The SQL Server variant of the installed layout: one product dir holding the AppServer's and the gateway's meta/sql.  The
//installer (apps/OpcHub/setup) ships args/install, which is sqlite; this one is reached by hand - `-include=args/install-sqlServer`
//on the service's command line, Jde.DB.Odbc.dll beside the exe, a System DSN `jde` - see setup/README.md.  paths-common
//only - the service starts without -tests, so no ext vars.
paths + {
	local args = self,
	local hubDir = args.companyDir+"/OpcHub",
	sqlType: "sqlServer",
	logsDir: hubDir,
	logFile: { keep: 3 }, //as args/install: the previous starts' logs kept beside the file - see the comment there.
	access:{ trustedCertDirs: [ args.certsDir("OpcServer") ] }, //as args/install: the installed products only - see the comment there.
	dbServers: {
		dataPaths: [hubDir+"/sql"],
		scriptPaths: [hubDir+"/sql"],
		localhost:{
			driver: "$(ExeDir)/Jde.DB.Odbc.dll", //beside the exe, wherever the installer put it (settings.cpp builtIns) - not companyDir, a different root.
			connectionString: "DSN=jde",
			username: null,
			password: null,
			schema: null,
			catalogs: {
				jde: {
					schemas:{
						acc:{
							access:{ meta: hubDir+"/access-meta.jsonnet", ql: hubDir+"/access-ql.jsonnet", prefix: "" }
						},
						app:{
							app:{ meta: hubDir+"/app-meta.jsonnet", prefix: "" }
						},
						opc:{
							gateway:{ meta: hubDir+"/opcGateway-meta.jsonnet", prefix: "" }
						}
					}
				}
			}
		}
	}
}

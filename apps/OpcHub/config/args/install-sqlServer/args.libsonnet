//The SQL Server variant of the installed layout.  The installer (apps/OpcHub/setup) ships args/install, which is sqlite; this one
//is reached by hand - `-include=args/install-sqlServer` on the service's command line, Jde.DB.Odbc.dll beside the exe, a System
//DSN `jde` - see setup/README.md.  args/install and only the database overridden:  a copy of its other keys drifted - it never
//gained #3's siteDir, so the switched hub served no Web UI (reviews/install-issues.md #63) - and its logs, certificates, Web UI,
//OAuth client id and host names are the same install's.  Edit those there;  it sits beside this dir in an install too.
(import '../install/args.libsonnet') + {
	local args = self,
	local hubDir = args.companyDir+"/OpcHub",
	sqlType: "sqlServer",
	dbServers: { //replaces args/install's whole block:  nothing of sqlite's catalogs carries over
		dataPaths: [hubDir+"/sql"],
		scriptPaths: [hubDir+"/sql-sqlServer"], //the T-SQL views and procs, copied by hand:  never sql/, which the installer recreates on every reinstall and fills with the sqlite scripts - it wiped these, and a sqlite trigger with no T-SQL twin was sent to SQL Server (reviews/m4-closing.md #18, #19).  dataPaths stays sql/: the seeds are the installer's, and dialect-free.
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

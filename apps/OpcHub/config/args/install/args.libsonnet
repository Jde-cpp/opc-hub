local paths = import '../../../../../libs/db/config/paths-common.libsonnet';
//The installed layout (apps/OpcHub/setup): one product dir holding the AppServer's and the gateway's meta/sql, one sqlite
//file beside them.  paths-common only - the service starts without -tests, so no ext vars, which is why sqlite-common
//(buildTarget/logsDir/windows/path) is not imported and its localhost/schema shapes are spelled out here instead.
//`$(ExeDir)` is the dir of the running exe (settings.cpp builtIns): the driver and the proc MODULEs ship beside it,
//wherever the installer put it - Program Files, a per-user Programs dir, /opt/jde-cpp/opchub - so no root is hardcoded,
//and `$(LibPrefix)`/`$(LibExt)` spell the platform's library name (Jde.DB.Sqlite.dll, libJde.DB.Sqlite.so), so the
//Windows installer (setup/) and the Linux package (setup/linux) ship this one file.  The SQL Server variant is
//args/install-sqlServer.
paths + {
	local args = self,
	local hubDir = args.companyDir+"/OpcHub", //companyDir is the one spelling of the company root - see paths-common.
	local lib( name ) = "$(ExeDir)/$(LibPrefix)"+name+"$(LibExt)",
	local appServerDll = lib( "Jde.DB.Sqlite.AppServer" ),
	sqlType: "sqlite",
	logsDir: hubDir,
	//The Web UI the installer put beside the exe's dir (Web on windows - the file system does not mind the case - web on linux),
	//served by the hub at http://<host>:1967/ (Opc.Hub.jsonnet http.site).
	siteDir: "$(ExeDir)/../web",
	//The Web UI's Google login (a provider the "OPC UA Server" component seeds - access_google.mutation): the OAuth 2.0 client
	//id the site's origin is registered under, served to the page by GET /GoogleAuthClientId.  The default is the project's
	//own id, whose authorized JavaScript origins are its dev ports on localhost - a site browsed from anywhere else needs its
	//own (Google Cloud console > APIs & Services > Credentials > OAuth client ID, Web application, authorized JavaScript
	//origin http://<host>:1967 - the page's own origin, the hub's), set here and the service restarted.  A reinstall overwrites
	//this file - keep a copy.
	googleAuthClientId: "445012155442-1v8ntaa22konm0boge6hj5mfs15o9lvd.apps.googleusercontent.com",
	//The hub's web certificate (Opc.Hub.jsonnet http.ssl).  Its names are localhost, this machine's name and 127.0.0.1; hostNames
	//adds the others a browser or a split OpcServer reaches the hub by - a fully qualified name, an alias - as DNS entries.  A
	//change re-issues the certificate on the same key at the next start.
	hostNames: [],
	//Your own certificate in place of the self-signed one - a CA your browsers trust:  merged into the config's certificate and
	//privateKey blocks.  managed:false uses the pair as found and never issues or replaces it; both files must exist or the
	//service does not start; the public key file the hub's identity reads is derived from the private key.  Uncomment and
	//point at the files:
	//certificate: { managed: false, path: "C:/ProgramData/Jde-Cpp/OpcHub/ssl/certs/OpcHub.pem" },
	//privateKey: { path: "C:/ProgramData/Jde-Cpp/OpcHub/ssl/private/OpcHub.pem", passcode: "" },
	dbServers: {
		dataPaths: [hubDir+"/sql"],
		scriptPaths: [hubDir+"/sql"],
		localhost:{
			driver: lib( "Jde.DB.Sqlite" ),
			connectionString: null,
			username: null,
			password: null,
			schema: null,
			catalogs: {
				master: { // n/a for sqlite - the only real field is the db path.
					path: hubDir+"/OpcHub.db",
					schemas:{
						dbo:{ // n/a for sqlite
							access:{ meta: hubDir+"/access-meta.jsonnet", ql: hubDir+"/access-ql.jsonnet", prefix: "access_", dynamicLib: appServerDll },
							app:{ meta: hubDir+"/app-meta.jsonnet", prefix: "app_", dynamicLib: appServerDll },
							gateway:{ meta: hubDir+"/opcGateway-meta.jsonnet", prefix: "gateway_", dynamicLib: lib( "Jde.DB.Sqlite.OpcGateway" ) }
						}
					}
				}
			}
		}
	}
}

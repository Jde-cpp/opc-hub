local paths = import '../../../../../libs/db/config/paths-common.libsonnet';
//The installed layout (apps/OpcHub/setup, the "OPC UA Server" component): meta files flat in the product dir, the nodesets
//beside them, one sqlite file.  paths-common only - the service starts without -tests, so no ext vars (which is why
//sqlite-common is not imported: it binds buildTarget/logsDir/windows/path).  Loaded through Opc.Server.Install.jsonnet,
//which replaces the base config's dev-shaped fields (nodeset paths, credentials, the login TLS anchor) from here.
paths + {
	local args = self,
	local serverDir = args.companyDir+"/OpcServer", //companyDir is the one spelling of the company root - see paths-common.
	sqlType: "sqlite",
	logsDir: serverDir,
	nodesetsDir: serverDir+"/nodesets", //read by Opc.Server.Install.jsonnet.
	access: {
		trustedCertDirs: [
			//Production products only - never a test product's dir.  Every cert under these dirs opens a secured UA session.
			//Only what the installer ships: the dev args also list a split gateway's dir and the PLC emulator's
			//(apps/OpcServer/emulator), which no install creates - each a warning in every log until it exists
			//(reviews/install-issues.md, "Noise in a production log").  Either is its dir added here
			//(args.certsDir("OpcGateway"), args.certsDir("PlcEmulator")) and the service restarted.
			args.certsDir( "OpcHub" ) //Jde.Opc.Hub - the gateway role's OPC client certs live under its own product dir.
		]
	},
	dbServers: {
		dataPaths: [],
		scriptPaths: [],
		localhost:{
			driver: "$(ExeDir)/$(LibPrefix)Jde.DB.Sqlite$(LibExt)", //beside the exe, wherever the installer put it, spelled for the platform (Jde.DB.Sqlite.dll / libJde.DB.Sqlite.so) - settings.cpp builtIns.
			connectionString: null,
			username: null,
			password: null,
			schema: null,
			catalogs: {
				master: { // n/a for sqlite - the only real field is the db path.
					path: serverDir+"/OpcServer.db",
					schemas:{
						_appServer:{
							//No dynamicLib: this process loads no access twins - the '_' prefix makes SqliteDataSource skip the schema when loading proc dlls.
							access:{ meta: serverDir+"/access-meta.jsonnet", ql: serverDir+"/access-ql.jsonnet", prefix: "access_" }
						},
						dbo:{ // n/a for sqlite
							//dynamicLib null, not absent: the OpcServer schema owns no tables and no native procs - its address space lives in NodeSet2 xml.
							opc:{ meta: serverDir+"/opcServer-meta.jsonnet", prefix: "opc_", dynamicLib: null }
						}
					}
				}
			}
		}
	}
}
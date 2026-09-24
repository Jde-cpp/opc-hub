// The SQL Server dev args, Windows only (the odbc driver builds nowhere else): `-include=args/sqlServer`.  args-common
// binds the ext vars and the roots; the connection is DSN=<debug|rls> by build target with the odbc driver from the
// build's bin, the `opc` schema under dbo with the `opc_` prefix, and the access schema mounted under `_appServer`
// (meta and ql, no prefix).  No scripts: the opc meta declares only the nodeIds resource.
local common = import '../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	sqlType: "sqlServer",
	instanceName: "OpcServer."+args.sqlType+"."+args.buildTarget,
	access: {
		trustedCertDirs: [
			//Production products only - never a test product's dir.  Every cert under these dirs opens a secured UA session,
			//and wherever this list is also the enrollment anchor (the test hosts, Jde.Opc.Hub) enrolls a user named by its
			//CN; the test binaries anchor their own dirs in their own configs (Opc.Server.Tests.jsonnet, Opc.Tests.jsonnet).
			args.certsDir( "OpcGateway" ),
			args.certsDir( "OpcHub" ), //Jde.Opc.Hub - the gateway role's OPC client certs live under its own product dir.
			args.certsDir( "PlcEmulator" ) //apps/OpcServer/emulator - its UA client cert.
		],
	},
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
					schemas:{
						_appServer:{
							access:{
								meta: args.repoSourceDir+"/libs/access/config/access-meta.jsonnet",
								ql: args.repoSourceDir+"/libs/access/config/access-ql.jsonnet",
							},
						},
						dbo:{
							opc:{
								meta: args.repoSourceDir+"/apps/OpcServer/config/opcServer-meta.jsonnet",
								prefix: "opc_"  //test with null prefix, debug with prefix
							},
						}
					}
				}
			}
		}
	}
}
local common = import '../../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	instanceName: "debug-windows",
	sqlType: "sqlServer",
	dbServers: {
		localhost:{
			driver: args.repoBuildDir+"/bin/Jde.DB.Odbc.dll",
			connectionString: "DSN=OpcTestsDebug",
			username: null,
			password: null,
			schema: null,
			catalogs: {
				test_opc_debug: {
					schemas:{
						access:{
							access:{
								meta: args.repoSourceDir+"/libs/access/config/access-meta.jsonnet"
							}
						},
						app:{
							app:{
								meta: args.repoSourceDir+"/apps/AppServer/config/app-meta.jsonnet",
							}
						},
						gateway:{
							gateway:{
								meta: args.repoSourceDir+"/apps/OpcGateway/config/opcGateway-meta.jsonnet",
							}
						},
						opc:{
							opc:{
								meta: args.repoSourceDir+"/apps/OpcServer/config/opcServer-meta.jsonnet",
							},
						}
					}
				}
			}
		}
	}
}
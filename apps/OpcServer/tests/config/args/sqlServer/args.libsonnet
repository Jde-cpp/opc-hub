local common = import '../../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	sqlType: "sqlServer",
	dbServers: {
		localhost:{
			driver: args.repoBuildDir+"/bin/Jde.DB.Odbc.dll",
			connectionString: "DSN=OpcServerTestsDebug",
			username: null,
			password: null,
			schema: null,
			catalogs: {
				test_opc_server_debug: {
					schemas:{
						acc:{
							access:{
								meta: "$(JDE_DIR)/libs/access/config/access-meta.jsonnet"
							}
						},
						app:{
							app:{
								meta: "$(JDE_DIR)/apps/AppServer/config/app-meta.jsonnet",
							}
						},
						opc:{
							opc:{
								meta: "$(JDE_DIR)/apps/OpcServer/config/opcServer-meta.jsonnet",
							},
						}
					}
				}
			}
		}
	}
}
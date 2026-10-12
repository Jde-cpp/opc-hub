local common = import '../../../../../../libs/db/config/args-common.libsonnet';
common + {
	local args = self,
	instanceName: "debug-windows",
	sqlType: "sqlServer",
	opc:{
		urn: "urn:open62541.server.application",
		url: "opc.tcp://127.0.0.1:4940"
	},
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
								meta: "$(JDE_DIR)/libs/access/config/access-meta.jsonnet"
							}
						},
						app:{
							app:{
								meta: "$(JDE_DIR)/apps/AppServer/config/app-meta.jsonnet",
							}
						},
						gateway:{
							gateway:{
								meta: "$(JDE_DIR)/apps/OpcGateway/config/opcGateway-meta.jsonnet",
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
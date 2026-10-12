local common = import '../../../../../../libs/db/config/sqlite-common.libsonnet';
common + {
	opc:{
		urn: "urn:open62541.server.application",
		url: "opc.tcp://127.0.0.1:4940"
	},
	dbServers: {
		localhost: common.localhost({
			dbo:{ // n/a for sqlite
				access: common.access(),
				app: common.app(),
				opc: common.opcSchema(),
			}
		})
	}
}

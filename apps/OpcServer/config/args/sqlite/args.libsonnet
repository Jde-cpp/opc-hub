// The sqlite dev and test args: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted, which is what ctest
// passes).  sqlite-common binds the ext vars (-tests/-ctest) and the one-file catalog; this file mounts the access
// schema (meta and ql, under `_appServer`: the `_` prefix means no twins and no proc module are loaded here) and the
// `opc` schema, whose meta declares only the nodeIds resource, so there are no procs and no scripts.  The trust list
// below is the same in every dialect.
local common = import '../../../../../libs/db/config/sqlite-common.libsonnet';
common + {
	local args = self,
	local cwd = std.extVar("cwd"),
	instanceName: "OpcServer."+args.sqlType+"."+args.buildTarget,
	access: {
		trustedCertDirs: [
			//Production products only - never a test product's dir.  Every cert under these dirs opens a secured UA session,
			//and wherever this list is also the enrollment anchor (the test hosts, Jde.Opc.Hub) enrolls a user named by its
			//CN; the test binaries anchor their own dirs in their own configs (Opc.Server.Tests.jsonnet, Opc.Tests.jsonnet).
			common.certsDir( "OpcGateway" ),
			common.certsDir( "OpcHub" ), //Jde.Opc.Hub - the gateway role's OPC client certs live under its own product dir.
			common.certsDir( "PlcEmulator" ) //apps/OpcServer/emulator - its UA client cert.
		]
	},
	dbServers: {
		dataPaths: [],
		scriptPaths: [],
		localhost: common.localhost({
			_appServer:{
				//test debug with schema, debug with default schema ie dbo.  No dynamicLib: this process loads no
				//access twins - the '_' prefix makes SqliteDataSource skip the schema when loading proc dlls.
				access:{ meta: common.accessMeta, ql: common.accessQL, prefix: "access_" },
			},
			dbo:{ // n/a for sqlite
				opc: common.opcSchema(),
			}
		})
	}
}

// The sqlite dev and test args: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted, which is what ctest
// passes).  sqlite-common binds the ext vars (-tests/-ctest) and the one-file catalog; this file mounts the access
// schema (meta and ql, under `_appServer`: the `_` prefix means no twins and no proc module are loaded here) and the
// `opc` schema, whose meta declares only the nodeIds resource, so there are no procs and no scripts.  The instance
// name and the trust list are opcServer-common's, as in every dialect.
local common = import '../../../../../libs/db/config/sqlite-common.libsonnet';
local server = import '../opcServer-common.libsonnet';
common + {
	local args = self,
	instanceName: server.instanceName( args ),
	access: { trustedCertDirs: server.trustedCertDirs( args ) },
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
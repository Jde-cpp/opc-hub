// The sqlite dev and test args: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted, which is what ctest
// passes).  sqlite-common binds the ext vars (-tests/-ctest) and the one-file catalog; this file mounts the gateway
// schema in it, the access meta for its type definitions only (`_access`: no twins, no procs), and points scriptPaths
// at the .sql views - the procs are native, Jde.DB.Sqlite.OpcGateway, loaded through the schema's dynamicLib.
local common = import '../../../../../libs/db/config/sqlite-common.libsonnet';
common + {
	local args = self,
	dbServers: {
		scriptPaths: [args.repoSourceDir + "/apps/OpcGateway/config/sql/"+args.sqlType],
		localhost: common.localhost({
			_access:{
				//meta only: mounted for its type definitions, no prefix and no twins loaded here - the '_' prefix
				//makes SqliteDataSource skip the schema when loading proc dlls.
				access:{ meta: common.accessMeta }
			},
			dbo:{ // n/a for sqlite
				gateway: common.gateway(),
			}
		})
	}
}

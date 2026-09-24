// The sqlite dev and test args: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted, which is what ctest
// passes). sqlite-common binds the ext vars (-tests/-ctest) and the one-file catalog; this file mounts the access and
// app schemas in it and points scriptPaths at the .sql views - the procs are native, Jde.DB.Sqlite.AppServer, loaded
// through each schema's dynamicLib.
local common = import '../../../../../libs/db/config/sqlite-common.libsonnet';
common + {
	local args = self,
	dbServers: {
		scriptPaths: [
			args.repoSourceDir+"/libs/access/config/sql/sqlite",
			args.repoSourceDir+"/apps/AppServer/config/sql/sqlite",
		],
		dataPaths: [
			args.repoSourceDir+"/apps/AppServer/config",
			args.repoSourceDir+"/libs/access/config"
		],
		localhost: common.localhost({
			dbo:{ // n/a for sqlite
				access: common.access(),
				app: common.app(),
			}
		})
	},
}

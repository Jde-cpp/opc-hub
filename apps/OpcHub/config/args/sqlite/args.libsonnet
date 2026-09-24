local common = import '../../../../../libs/db/config/sqlite-common.libsonnet';
//The sqlite dev args: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted); sqlite-common binds the ext vars
//(-tests/-ctest) and the one-file catalog.  access + app + gateway in one sqlite file: both proc MODULEs
//(Jde.DB.Sqlite.AppServer, Jde.DB.Sqlite.OpcGateway) get loaded.
common + {
	local args = self,
	dbServers: {
		scriptPaths: [
			args.repoSourceDir+"/libs/access/config/sql/sqlite",
			args.repoSourceDir+"/apps/AppServer/config/sql/sqlite",
			args.repoSourceDir+"/apps/OpcGateway/config/sql/sqlite"
		],
		dataPaths: [
			args.repoSourceDir+"/apps/AppServer/config",
			args.repoSourceDir+"/libs/access/config"
		],
		localhost: common.localhost({
			dbo:{ // n/a for sqlite
				access: common.access(),
				app: common.app(),
				gateway: common.gateway()
			}
		})
	}
}

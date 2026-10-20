local common = import '../../../../../libs/db/config/sqlite-common.libsonnet';
local hub = import '../hub-common.libsonnet';
//The sqlite dev args: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted); sqlite-common binds the ext vars
//(-tests/-ctest) and the one-file catalog.  access + app + gateway in one sqlite file: both proc MODULEs
//(Jde.DB.Sqlite.AppServer, Jde.DB.Sqlite.OpcGateway) get loaded.
common + {
	local args = self,
	dbServers: {
		scriptPaths: hub.scriptPaths( args ),
		dataPaths: hub.dataPaths( args ),
		localhost: common.localhost({
			dbo:{ // n/a for sqlite
				access: common.access(),
				app: common.app(),
				gateway: common.gateway()
			}
		})
	}
}
//The sqlite args for Jde.Access.Tests: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted, which is what
//ctest passes).  sqlite-common binds the ext vars and the one-file catalog; the access schema mounts with the `access_`
//prefix and the Jde.DB.Sqlite.AppServer proc module, which carries the access procs.
local common = import '../../../../../../libs/db/config/sqlite-common.libsonnet';
common + {
	dbServers: {
		localhost: common.localhost({
			dbo:{ // n/a for sqlite
				access: common.access(),
			}
		})
	},
}

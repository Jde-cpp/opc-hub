//The sqlite args for Jde.QL.Tests: `-include=args/sqlite -arg path=<file>` (`:memory:` if omitted, which is what ctest
//passes).  sqlite-common binds the ext vars and the one-file catalog; the qlTests schema (../../qlTests-meta.jsonnet) mounts
//with the `qlt_` prefix and the Jde.DB.Sqlite.QLTests proc module (../../sql/sqlite), which carries its insert twins.
local common = import '../../../../../../libs/db/config/sqlite-common.libsonnet';
common + {
	dbServers: {
		localhost: common.localhost({
			dbo:{ // n/a for sqlite
				qlTests: {
					meta: common.repoSourceDir+"/libs/ql/tests/config/qlTests-meta.jsonnet",
					prefix: "qlt_",
					dynamicLib: common.lib( "Jde.DB.Sqlite.QLTests", "/libs/ql/tests/config/sql/sqlite" )
				}
			}
		})
	},
}
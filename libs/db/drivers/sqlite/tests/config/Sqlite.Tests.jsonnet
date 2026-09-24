//Jde.DB.Sqlite.Tests: the sqlite driver - schema sync, ops and awaits, the proc registry, connections.  Inherently
//sqlite, so no args/ dir: the clusters are spelled out here on sqlite-common - `memory` and `file`
//(<cwd>/sqlite-tests.db) with the access, app, opc and gateway schemas, plus the bare busyHolder/busyWaiter pair for
//the busy-timeout test and `wedge`, whose open must fail.  The `*/SchemaTests.*/file` filter shape is the parameterised
//suite name: <suite>.<test>/<cluster>.
local common = import '../../../../config/sqlite-common.libsonnet';
local logsDir = common.logsDir;
local repoSourceDir = common.repoSourceDir;
local lib = common.lib;
local cluster(path) = { //one backend; instantiated per-path as the 'memory' and 'file' clusters below.
	driver: lib( "Jde.DB.Sqlite", "/libs/db/drivers/sqlite/lib" ),
	catalogs: {
		testDb: { // n/a for sqlite
			path: path,
			schemas:{
				master:{ // n/a for sqlite
					access: common.access(),
					app: common.app(),
					opc: common.opcSchema(),//tables-less since the OpcServer address space moved to NodeSet2 xml - it keeps SyncSchema honest on an empty schema.
					gateway: common.gateway( {prefix: "gtw_"} )
				}
			}
		}
	}
};
{
	local args = self,
	testing:{
		tests:: "*/SchemaTests.*/file"
	},
	instanceName: "SqliteTests",
	dbServers:{//clusters
		scriptPaths: [
			repoSourceDir+"/libs/access/config/sql/sqlite",
			repoSourceDir+"/apps/AppServer/config/sql/sqlite"
		],
		dataPaths: [
			repoSourceDir+"/apps/AppServer/config",
			repoSourceDir+"/libs/access/config"
		],
		sync:: true,
		memory: cluster(":memory:"),
		file: cluster( std.extVar("cwd")+"/sqlite-tests.db" ),
		//#45 only: two connections on one file, so a busy timeout has something to wait for.  Their own file, so nothing
		//else in the suite sees the write lock, and no app schemas - they run raw statements, never a query.
		local bare( path, extra={} ) = {
			driver: lib( "Jde.DB.Sqlite", "/libs/db/drivers/sqlite/lib" ),
			catalogs: { testDb: { path: path, schemas: { master: {} } } + extra }
		},
		busyHolder: bare( std.extVar("cwd")+"/sqlite-busy.db" ),
		busyWaiter: bare( std.extVar("cwd")+"/sqlite-busy.db", {busyTimeoutMs: 500} ),
		//ConnectionTests only: the open must fail. No app schemas - it never runs a query, and the meta jsonnets cluster() pulls in cost seconds to evaluate.
		wedge: {
			driver: lib( "Jde.DB.Sqlite", "/libs/db/drivers/sqlite/lib" ),
			catalogs: { testDb: { path: "/nonexistent-dir-for-tests/wedge.db", schemas: { master: {} } } }
		}
	},
	logging:{
		spd:{
			tags: {
				default: "Information",
				app: "Trace",
				exception: "Trace",
				test: "Trace",
				settings: "Debug",
				sql: "Debug"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		}
	},
	workers:{
		executor: {threads: 2}
	}
}

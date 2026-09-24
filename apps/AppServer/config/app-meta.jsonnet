// The `app` schema: programs, their instances and hosts, the connections each run registers, the log levels and the
// per-instance tag levels the site's log settings set.  Mounted by name from an args file's catalog (`app:{ meta: <this
// file>, prefix: "app_" }`) - this app's args, the hub's, and the test configs of every app that embeds an AppServer -
// and synced by DB::SyncSchema on start.  `common-meta.libsonnet` beside it is a link the build makes to
// libs/db/config/common-meta.libsonnet.  The seed rows are app.mutation; the custom insert procs are
// config/sql/<dialect>, compiled into Jde.DB.Sqlite.AppServer for sqlite.
local common = import 'common-meta.libsonnet';
{
	local tables = self.tables,
	views:{
		connections_ql:{
			columns: {
				connectionId: tables.connections.columns.connectionId,
				instanceId: common.types.uint+{ i:1 }, // plain data, no sequence/sk
				instanceName: tables.instances.columns.name,
				programName: tables.programs.columns.name,
				hostName: tables.hosts.columns.name,
				created: tables.connections.columns.created,
				deleted: tables.connections.columns.deleted,
				pid: tables.connections.columns.pid
			},
			naturalKeys: [["connection_id"]]
		}
	},
	tables:{
		programs:{
			columns: {
				programId: common.smallSequenced,
				name: common.valuesColumns.name,
				attributes: common.slugColumns.attributes
			},
			customInsertProc: true,
			ops: ["None"]
		},
		instances:{
			columns: {
				instanceId: common.pkSequenced,
				programId: tables.programs.columns.programId+{ pkTable: "programs", i:1, sk:null },
				name: common.valuesColumns.name+{ i:2 },
				hostId: tables.hosts.columns.hostId+{ pkTable: "hosts", i:3, sk:null },
			},
			customInsertProc: true,
			naturalKeys:[["program_id", "name"]]
		},
		logLevels:{
			columns: {
				levelId: common.types.uint8+{ sk:0, i:0 },
				name: common.valuesColumns.name+{ i:1 }
			},
			naturalKeys:[["name"]],
			ops: ["None"]
		},
		instanceTagLevels:{
			columns: {
				instanceId: tables.instances.columns.instanceId+{ pkTable: "instances", sk:0, i:0 },
				type: common.valuesColumns.name+{ sk:1, i:1 },
				tag: common.types.ulong+{ sk:2, i:2 },
				levelId: tables.logLevels.columns.levelId+{ pkTable: "log_levels", i:3, sk:null }
			},
		},
		connections:{
			columns: {
				connectionId: common.pkSequenced,
				instanceId: tables.instances.columns.instanceId+{ pkTable: "instances", i:1, sk:null },
				created: common.slugColumns.created+{ i:2 },
				deleted: common.slugColumns.deleted+{ i:3 },
				pid: common.types.ulong+{ i:4 },
			},
			customInsertProc: true,
			qlView: "connections_ql",
			ops: ["None"]
		},
		hosts:{
			columns: {
				hostId: common.smallSequenced,
				name: common.valuesColumns.name
			},
			naturalKeys:[["name"]],
			ops: ["None"]
		},
	}
}
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
				resourceSchema: common.types.varchar+{ length:32, nullable:true, i:4, comment: "the instance's own access schema (opc.install), from its registration's auth_resource - its forwarded log lines and tag levels answer to <resourceSchema>/logs, not app/logs" }
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
		//The shift calendar (docs/calendar/spec.md *Tables*):  every stored time a UTC dateTime, a date the instant its production
		//day starts in the calendar's zone, the shift columns wall-clock minutes.  Every edit of the four is routed by
		//App::Server::CustomMutation, the hub's HubQL too, to Calendar::Server::CalendarMAwait (libs/calendar/src/server), which
		//validates the row with the library, reads a zone-less time in the calendar's zone, makes a new calendar's first
		//pattern, re-anchors the children of a zone or day-start change and purges a row's children before it.  Column names
		//avoid the words a dialect reserves, which the DDL emits unquoted (Column.cpp):  `effective` not `from`, `finish` not
		//`end`, `is_open` not `open`.
		calendars:{
			comment: "A shift calendar:  a time zone and production-day start, with dated patterns of shifts and dated exceptions",
			columns: {
				calendarId: common.pkSequenced,
				timeZone: common.types.varchar+{ length:64, i:1, comment:"An IANA zone name, America/New_York:  the shifts' wall clock, and the zone a zone-less time is read in." },
				dayStart: common.types.uint16+{ default:0, i:2, comment:"Minutes after local midnight the production day begins, under 1440:  with 360, Monday's day runs 06:00 Monday to 06:00 Tuesday, and a night shift that starts at 22:00 is Monday's." }
			}+common.slugColumns,
			naturalKeys: common.slugNKs
		},
		calendarPatterns:{
			comment: "A calendar's shifts from a date:  a cycle of cycleDays days, 7 by default, anchored on cycleStart.  In effect from its effective to the next pattern's",
			columns: {
				calendarPatternId: common.pkSequenced,
				calendarId: tables.calendars.columns.calendarId+{ pkTable: "calendars", sk:null, updateable:false, i:1 },
				effective: common.types.dateTime+{ nullable:true, i:2, comment:"The instant the production day the pattern is in effect from begins in the calendar's zone;  null on the calendar's first pattern, in effect from the beginning of time.  A soft-deleted pattern keeps its (calendar_id, effective) until its purge." },
				cycleDays: common.types.uint8+{ default:7, i:3, comment:"The cycle's length, 1 to 31." },
				cycleStart: common.types.dateTime+{ nullable:true, i:4, comment:"The start of the production day that is cycle day 0;  null for a 7-day cycle whose day 0 is Monday." },
				created: common.slugColumns.created+{ i:5 },
				deleted: common.slugColumns.deleted+{ i:6 }
			},
			naturalKeys: [["calendar_id", "effective"]]
		},
		calendarShifts:{
			comment: "A pattern's shift:  the cycle days it runs, its start and length as wall-clock minutes, and its breaks.  No deleted - a shift leaves by purge",
			columns: {
				calendarShiftId: common.pkSequenced,
				calendarPatternId: tables.calendarPatterns.columns.calendarPatternId+{ pkTable: "calendar_patterns", sk:null, updateable:false, i:1 },
				name: common.valuesColumns.name+{ i:2 },
				days: common.types.uint+{ i:3, comment:"A bitmask whose bit i is cycle day i, Monday bit 0 on the default cycle.  32 bits:  the web's numbers are exact to 53, and a ulong would not survive the round trip." },
				start: common.types.uint16+{ i:4, comment:"Minutes after the production day's start, under 1440." },
				minutes: common.types.uint16+{ i:5, comment:"The shift's length, 1 to 1440.  One that runs past the day's end belongs to the day it starts on." },
				breaks: common.types.varchar+{ length:2048, nullable:true, i:6, comment:'A json list of {"start","minutes"} offsets from the shift\'s start, [{"start":240,"minutes":30}];  null for none.' }
			},
			naturalKeys: [["calendar_pattern_id", "name"]]
		},
		calendarExceptions:{
			comment: "A dated range that closes a calendar's planned time, a holiday or a shutdown, or opens it, an extra Saturday.  Closed beats open",
			columns: {
				calendarExceptionId: common.pkSequenced,
				calendarId: tables.calendars.columns.calendarId+{ pkTable: "calendars", sk:null, updateable:false, i:1 },
				name: common.valuesColumns.name+{ i:2 },
				start: common.types.dateTime+{ i:3, comment:"The instant the range begins.  Entered as a local time in the calendar's zone, or with Z or an offset as the instant it names." },
				finish: common.types.dateTime+{ i:4, comment:"The instant the range ends, after start, entered as start is." },
				isOpen: common.types.bit+{ default:false, i:5, comment:"False closes the range;  true opens it as planned time with no shift." },
				created: common.slugColumns.created+{ i:6 },
				deleted: common.slugColumns.deleted+{ i:7 }
			}
		}
	},
	//No table behind either, as the OpcServer's nodeIds.  `logs`:  the AppServer's log - its own lines and every forwarded line
	//whose instance has no resourceSchema - plus its log settings and the stored tag levels of those instances;  each app declares
	//its own, so an OpcServer's is `opc.<instance>/logs`.  `sessions`:  the web sessions of this process and of every instance
	//(sessions{…}, session(id:…), sessions(instanceId:…) - SessionGraphQL, InstanceSessionsAwait) take Read, and purgeSession
	//Administer - but for the caller's own session and one it signed in, a stand-alone gateway's at its logout.  Not the gateway's
	//`sessions`, its OPC UA sessions.
	resources:{
		logs:{ ops:["Read","Update","Administer"] },
		sessions:{ ops:["Read","Administer"] }
	}
}
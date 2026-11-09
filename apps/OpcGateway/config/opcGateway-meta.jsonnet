// The `gateway` schema: server_connections, the OPC servers the site connects to, plus `sessions` and `search`,
// read-only names with no table behind them that the QL grafts live data onto (opcSessions, search).  Mounted by name
// from an args file's catalog (`gateway:{ meta: <this file>, prefix: … }`) - this app's args, the hub's, and the test
// configs that embed a gateway - and synced by DB::SyncSchema on start.  `common-meta.libsonnet` beside it is a link
// the build makes to libs/db/config/common-meta.libsonnet.  The insert proc is config/sql/<dialect>, compiled into
// Jde.DB.Sqlite.OpcGateway for sqlite.  Of the two .mutation files here, the installer applies
// release-opcServer.mutation (renamed gateway_opcServer.mutation: the bundled server as the default connection) and
// skips access-opcGateway.mutation, whose createRole shape the seed does not apply; the dev args list no dataPaths, so
// neither runs in a dev tree.
local common = import 'common-meta.libsonnet';
//Part 11's HistoricalDataConfiguration as columns, from column order `i`.
local thresholds( i ) = {
	exceptionDeviation: common.types.float+{ nullable:true, i:i, comment:"Part 11's ExceptionDeviation.  Null inherits:  a group node's from its template member, then its group, then none, which stores every change." },
	exceptionDeviationFormat: common.types.uint8+{ nullable:true, i:i+1, comment:"UA_ExceptionDeviationFormat's value:  0 AbsoluteValue, 1 PercentOfValue, 2 PercentOfRange, 3 PercentOfEURange.  Null inherits." },
	minTimeInterval: common.types.float+{ nullable:true, i:i+2, comment:"Part 11's MinTimeInterval, in milliseconds as UA's Duration is.  Null inherits." },
	maxTimeInterval: common.types.float+{ nullable:true, i:i+3, comment:"Part 11's MaxTimeInterval, the heartbeat, in milliseconds.  0 is off.  Null inherits." },
	stepped: common.types.bit+{ nullable:true, i:i+4, comment:"Part 11's Stepped.  Null inherits, and the default is true." }
};
//What a node's server says of it (docs/servers/spec.md *Address space*), from column order `i`:  read once the collector has the
//node's item and written to the row by HistGroupHost::Describe directly, at every create that changes them, so a reconnect
//refreshes them and a restart serves the address space from the rows before any source is up.  Neither insertable nor updateable:
//the QL never writes them.  Null until the server has described the node.
local attributes( i ) = {
	displayName: common.types.varchar+{ length:256, nullable:true, insertable:false, updateable:false, i:i, comment:"The node's DisplayName, its text alone:  empty where the server gives an empty one, null until it has been read." },
	description: common.types.varchar+{ length:2048, nullable:true, insertable:false, updateable:false, i:i+1, comment:"The node's Description, its text alone;  null for none." },
	dataType: common.types.varchar+{ length:672, nullable:true, insertable:false, updateable:false, i:i+2, comment:'The DataType served for the node, spelled as `node` is (HistNodeText):  the source\'s where that is a namespace-0 type, abstract or not, else the built-in its values encode as, the historian\'s rule - an enumeration Int32, a structure Structure.' },
	valueRank: common.types.int+{ nullable:true, insertable:false, updateable:false, i:i+3, comment:"The node's ValueRank:  -1 a scalar, 1 a one-dimensional array, and the rest of Part 3's values." },
	arrayDimensions: common.types.varchar+{ length:256, nullable:true, insertable:false, updateable:false, i:i+4, comment:"The node's ArrayDimensions as a json list, [4];  null for none." },
	accessLevel: common.types.uint8+{ nullable:true, insertable:false, updateable:false, i:i+5, comment:"The source's AccessLevel bits:  1 CurrentRead, 2 CurrentWrite, 4 HistoryRead, …" },
	euRange: common.types.varchar+{ length:128, nullable:true, insertable:false, updateable:false, i:i+6, comment:'The EURange property as json, {"low":0,"high":3000}, a non-finite bound as Part 6 spells it, "NaN", "Infinity" or "-Infinity";  null where the node has none.' },
	engineeringUnits: common.types.varchar+{ length:1024, nullable:true, insertable:false, updateable:false, i:i+7, comment:'The EngineeringUnits property, Part 8\'s EUInformation, as json:  {"namespaceUri":"http://www.opcfoundation.org/UA/units/un/cefact","unitId":4404,"displayName":"rpm","description":"revolutions per minute"};  null where the node has none.' },
	path: common.types.varchar+{ length:4096, nullable:true, insertable:false, updateable:false, i:i+8, comment:"The node's browse path from the Objects folder over the inverse HierarchicalReferences, in a template member's spelling (HistPathText, hist_template_nodes.path), each parent's segment with its node's id, its nsu left out where it is the segment's:  the endpoint's node for it (servers spec *Namespaces*);  null for a node outside that tree, deeper than 64 (ParentPathsAwait::MaxDepth), or with a browse name on the way whose name or namespace URI is empty." }
};
{
	local tables = self.tables,
	tables:{
		server_connections:{
			columns: {
				server_connection_id: common.pkSequenced,
				is_default: common.types.bit+{i:101, default:false},
				default_browse_ns: common.types.uint16+{i:102, nullable:true},
				certificate_uri: common.types.varchar+{nullable:true, length:2048,i:103},
				url: common.types.varchar+{length:2048, i:104},
				//docs/clients/spec.md *Connection settings*:  how the gateway's client connects, null meaning the gateway's own rule or setting.
				security_mode: common.types.varchar+{ nullable:true, length:16, i:105, comment:"The message security mode pinned for the channel, UA's name:  None, Sign or SignAndEncrypt.  Null keeps the floor:  SignAndEncrypt with a certificate_uri, None without.  Sign or SignAndEncrypt needs a certificate_uri (ServerCnnctnMAwait)." },
				security_policy: common.types.varchar+{ nullable:true, length:256, i:106, comment:"The security policy uri pinned for the channel, http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256;  one the gateway carries (ClientSecurity::SecuredPolicyUris), or None's.  Null takes the strongest the two sides share." },
				session_timeout: common.types.uint+{ nullable:true, i:107, comment:"The session timeout requested at CreateSession, in milliseconds as UA's Duration is (UA_ClientConfig::requestedSessionTimeout).  Null is open62541's default." },
				request_timeout: common.types.uint+{ nullable:true, i:108, comment:"How long a service call may take before the client fails it, in milliseconds (UA_ClientConfig::timeout), and the test connect's deadline.  Null is /gateway/requestTimeout." },
				ping_interval: common.types.uint+{ nullable:true, i:109, comment:"The keep-alive's period, in milliseconds (AsyncRequest::Ping).  Null is /gateway/pingInterval, or half of session_timeout where that is set alone and shorter." },
				ttl: common.types.uint+{ nullable:true, i:110, comment:"How long an idle client lives before the gateway closes it, in milliseconds.  Null is /gateway/ttl." },
				locale: common.types.varchar+{ nullable:true, length:256, i:111, comment:"The session's preferred locale ids, a comma-separated list, en-US,de (UA_ClientConfig::sessionLocaleIds):  a server with several locales answers DisplayName and Description in the first it has.  Null sends none." }
			}+common.slugColumns+{
				slug: common.slugColumns.slug+{updateable:false, comment:"The connection's identity everywhere but this table:  the url segment, the key the live UAClient sits under (UM), the sessions join key.  The QL refuses an update that names a non-updateable column, so a rename never leaves the running gateway keyed by a name the row no longer has.  Only here - users/roles/groups may rename."}
			},
			customInsertProc:true,
			naturalKeys: common.slugNKs
		},
		//The gateway historian's groups and templates (libs/historian/docs/spec.md, *Collection* and *Templates*).  a hist_group_nodes row id is the node's node_index in the group's files for good.  The live side
		//of every change here - the group's files, its monitored items - is src/hist, run by ql/HistGroupQLAwait, which GatewayQL routes every edit of these tables to.
		histTemplates:{
			comment: "A named node set, as browse paths from a root, that a group applies to one root on one server",
			columns: {
				histTemplateId: common.pkSequenced
			}+common.slugColumns,
			naturalKeys: common.slugNKs
		},
		histTemplateNodes:{
			comment: "A template's member:  a browse path relative to the group's root, with its thresholds",
			columns: {
				histTemplateNodeId: common.pkSequenced,
				histTemplateId: tables.histTemplates.columns.histTemplateId+{ pkTable: "hist_templates", sk:null, i:1 },
				path: common.types.varchar+{ length: 672, i:2, comment:'The member\'s browse path from the group\'s root over HierarchicalReferences, a json list of browse names by namespace URI:  [{"nsu":"urn:…","name":"Motor"},…].  672 characters:  with the fk, under mysql\'s 3072-byte key limit at four bytes a character, as access_resources\' criteria is.' }
			}+thresholds( 3 ),
			naturalKeys: [["hist_template_id", "path"]]
		},
		histGroups:{
			comment: "A node group the gateway historizes:  a template applied to one root on one server, or an ad hoc node list on one",
			columns: {
				histGroupId: common.pkSequenced,
				guid: common.types.guid+{ length: 16, updateable:false, i:1, comment:"Generated when the group is created (HistGroupMAwait), unique, and the stem of the group's files, so a group made after the database is restored never writes into an older group's files, and the criteria of the group's own access row (spec *Authorization*, #357), which no later group inherits either.  Not updateable:  a rename would orphan them. QL reads and writes its text, 0f8fad5b-d9cb-469f-a165-70867728950e, which names the files." },
				serverConnectionId: tables.server_connections.columns.server_connection_id+{ pkTable: "server_connections", sk:null, updateable:false, i:2, comment:"The connection's key, a foreign key as the access tables declare identity_id:  the database refuses to delete a connection that still has groups.  A group never spans servers." },
				histTemplateId: tables.histTemplates.columns.histTemplateId+{ pkTable: "hist_templates", sk:null, nullable:true, updateable:false, i:3 },
				root: common.types.varchar+{ length: 672, nullable:true, updateable:false, i:4, comment:'Where the template\'s paths start, an ExpandedNodeId as json with its namespace URI (HistNodeText):  {"nsu":"urn:…","i":5001}.  Null is the Objects folder.' }
			}+thresholds( 5 )+common.slugColumns,
			naturalKeys: [common.valuesNK, ["slug"], ["guid"]]
		},
		histGroupNodes:{
			comment: "A group's member, resolved from its template or added by hand; the row id is the node's index in the group's files",
			columns: {
				histGroupNodeId: common.pkSequenced,
				histGroupId: tables.histGroups.columns.histGroupId+{ pkTable: "hist_groups", sk:null, updateable:false, i:1 },
				serverConnectionId: tables.server_connections.columns.server_connection_id+{ pkTable: "server_connections", sk:null, updateable:false, i:2, comment:"The group's connection, repeated here for the unique index below:  a node belongs to at most one group per server, so its thresholds have one answer." },
				node: common.types.varchar+{ length: 672, updateable:false, i:3, comment:'An ExpandedNodeId as json with its namespace URI, never an index, so a server that renumbers its namespaces does not re-point history (HistNodeText):  {"nsu":"urn:…","i":6042}.' },
				histTemplateNodeId: tables.histTemplateNodes.columns.histTemplateNodeId+{ pkTable: "hist_template_nodes", sk:null, nullable:true, updateable:false, i:4 }
			}+thresholds( 5 )+attributes( 10 ),
			//The unique (connection, node) index.  No `deleted`:  a node leaves its group by purge, which frees the index for another
			//group, and rejoining is a new row and so a new node_index (spec *Collection*).
			naturalKeys: [["server_connection_id", "node"]]
		},
		//OEE on the historian (docs/oee/spec.md *Machines*, *Stops*, *Intervals*, *Reasons and codings*).  Every edit of the six is
		//routed by Gateway::CustomMutation, the hub's HubQL too, to ql/OeeMAwait, which checks the row with libs/oee's rules, refuses
		//every generic mutation of the engine's three - oeeStops, oeeIntervals, oeeCodings - and runs the live side in src/oee.  A time
		//the engine writes is a `long` of UA DateTime ticks, 100 ns from 1601 UTC, as the group's records are stamped:  a dateTime
		//column holds whole seconds on sqlite and mysql, and a stop begins and ends at a record's source time.  `finish` and `width`
		//are the spec's end and interval:  SQL Server reserves END and mysql INTERVAL, and column names are emitted unquoted.
		oeeMachines:{
			comment: "A machine:  one historian group whose members are given roles (oee_machine_signals), with its thresholds, its rows' width and its shift calendar",
			columns: {
				oeeMachineId: common.pkSequenced,
				histGroupId: tables.histGroups.columns.histGroupId+{ pkTable: "hist_groups", sk:null, updateable:false, i:1, comment:"The group whose records the engine reads, a foreign key and unique:  a group has at most one machine, deleted ones counted.  OeeMAwait refuses the group's delete and purge while the row stands;  the key is the backstop.  Not updateable:  purge the machine and create it again." },
				calendarId: common.types.uint+{ nullable:true, i:2, comment:"The AppServer calendar's id as a value, not a foreign key:  the calendar is another schema's row and, in a split deployment, another process's.  Checked against the calendar client's cache when set.  Null plans every minute." },
				idealCycle: common.types.float+{ nullable:true, i:3, comment:"Milliseconds per unit, as UA's Duration is.  Null measures no performance." },
				countUnit: common.types.varchar+{ length:64, nullable:true, i:4, comment:"What a count is, for the pages:  bottles, cases." },
				smallStop: common.types.uint+{ default:300000, i:5, comment:"Milliseconds:  a stop shorter than this is a small stop, a performance loss.  Applied when a figure is read." },
				idleTimeout: common.types.uint+{ nullable:true, i:6, comment:"Milliseconds without a count before a machine with no State signal is stopped, from its last count.  Required without a State signal." },
				width: common.types.uint+{ default:60000, i:7, comment:"The spec's interval:  the width of the machine's oee_intervals rows in milliseconds, 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30 or 60 minutes.  A change takes effect at the next boundary both widths share." },
				processed: common.types.dateTime+{ nullable:true, insertable:false, updateable:false, i:8, comment:"The end of the last interval the engine wrote, where a restart resumes:  the `from` a create names, to the second, now by default, then a boundary of the width.  Written by the engine alone." }
			}+common.slugColumns,
			naturalKeys: [common.valuesNK, ["slug"], ["hist_group_id"]]
		},
		oeeMachineSignals:{
			comment: "A machine's signal:  a member of its group in a role.  No deleted - a signal leaves by purge",
			columns: {
				oeeMachineSignalId: common.pkSequenced,
				oeeMachineId: tables.oeeMachines.columns.oeeMachineId+{ pkTable: "oee_machines", sk:null, updateable:false, i:1 },
				role: common.types.varchar+{ length:8, updateable:false, i:2, comment:"State, Total, Rate, Good, Reject or Reason;  one row per role." },
				histGroupNodeId: tables.histGroupNodes.columns.histGroupNodeId+{ pkTable: "hist_group_nodes", sk:null, i:3, comment:"The member, which must be the machine's group's:  a foreign key, so a member a machine uses cannot leave its group until the signal does.  OeeMAwait refuses the member's purge first." },
				running: common.types.varchar+{ length:1024, nullable:true, i:4, comment:"A State signal's values that mean running, a json list of Booleans, numbers and strings:  [2,3] for a code.  Null is [true]." },
				ratePeriod: common.types.uint+{ default:60000, i:5, comment:"A Rate signal's time unit in milliseconds:  60000 for a per-minute rate." }
			},
			naturalKeys: [["oee_machine_id", "role"]]
		},
		oeeStops:{
			comment: "The engine's:  a machine's stop, from the record that left running to the record that returned, or with unknown a span of unknown time.  No generic mutation",
			columns: {
				oeeStopId: common.pkSequenced,
				oeeMachineId: tables.oeeMachines.columns.oeeMachineId+{ pkTable: "oee_machines", sk:null, updateable:false, i:1 },
				start: common.types.long+{ i:2, comment:"UA ticks:  the source time of the record that left running, or of the Bad record or the gap's start." },
				finish: common.types.long+{ nullable:true, i:3, comment:"The spec's end, UA ticks;  null while the stop is in progress." },
				code: common.types.long+{ nullable:true, i:4, comment:"The Reason signal's value at the stop's start when that is non-zero, else its first non-zero value before the stop's end." },
				unknown: common.types.bit+{ default:false, i:5, comment:"True for a span of unknown time, which is not a stop:  the reads' sums never count it." }
			},
			naturalKeys: [["oee_machine_id", "start"]]
		},
		oeeIntervals:{
			comment: "The engine's:  one row per machine per interval, every one written.  An absent row means the engine has not reached it.  No generic mutation",
			columns: {
				oeeIntervalId: common.longSequenced,
				oeeMachineId: tables.oeeMachines.columns.oeeMachineId+{ pkTable: "oee_machines", sk:null, updateable:false, i:1 },
				start: common.types.long+{ i:2, comment:"UA ticks:  a boundary of the row's width, but for a machine's first row, which starts where its engine did." },
				ms: common.types.uint+{ i:3, comment:"The row's width in milliseconds." },
				run: common.types.uint+{ i:4, comment:"Milliseconds running;  run, stop and unknown sum to ms." },
				stop: common.types.uint+{ i:5, comment:"Milliseconds stopped." },
				unknown: common.types.uint+{ i:6, comment:"Milliseconds unknown." },
				total: common.types.float+{ i:7, comment:"The count in the row, a float since a Rate integrates fractions." },
				good: common.types.float+{ i:8, comment:"Signed as the signals give it:  a reject recorded a row after its count makes that row's negative, and a read nets a bucket." },
				reject: common.types.float+{ i:9 }
			},
			naturalKeys: [["oee_machine_id", "start"]]
		},
		oeeReasons:{
			comment: "A stop reason, one list per gateway:  its category and the code the Reason signal reports for it",
			columns: {
				oeeReasonId: common.pkSequenced,
				category: common.types.varchar+{ length:16, i:1, comment:"Unplanned, Planned or Excluded." },
				code: common.types.long+{ nullable:true, i:2, comment:"The Reason signal's value for it, unique among the reasons, deleted ones counted, since a deleted reason keeps resolving.  OeeMAwait's check, not an index:  SQL Server's unique index holds one null." }
			}+common.slugColumns,
			naturalKeys: common.slugNKs
		},
		oeeCodings:{
			comment: "An operator's reason over a range of a machine's time.  Written by codeOee and uncodeOee alone;  the engine never touches one.  No generic mutation, and no deleted",
			columns: {
				oeeCodingId: common.pkSequenced,
				oeeMachineId: tables.oeeMachines.columns.oeeMachineId+{ pkTable: "oee_machines", sk:null, updateable:false, i:1 },
				start: common.types.long+{ i:2, comment:"UA ticks.  A machine's codings do not overlap." },
				finish: common.types.long+{ i:3, comment:"The spec's end, UA ticks, after start." },
				oeeReasonId: tables.oeeReasons.columns.oeeReasonId+{ pkTable: "oee_reasons", sk:null, i:4 },
				comment: common.types.varchar+{ length:2048, nullable:true, i:5 },
				identityId: common.types.uint+{ i:6, comment:"The operator's identity as a value, the historian's identity rule:  never a key into the access schema, which a split gateway's database does not hold.  0 for an anonymous session." },
				userName: common.types.varchar+{ length:256, i:7, comment:"The operator's name as it was then." },
				created: common.slugColumns.created+{ i:8 }
			},
			naturalKeys: [["oee_machine_id", "start"]]
		}
	},
	//The gateway's other two Authorize::Test gates (reviews/m2-closing.md #8).  `opcSessions` and `search` are QL *system*
	//tables - live state, nothing in the database - so no table declared their resource, ResourceSyncAwait created no row, and
	//Test() passes whatever it finds no active row for:  neither gate could be enforced from the product, and with
	//gateway/serverConnections enforced an ungranted user was still told who holds which credential on which connection.
	//Declared outright, as the OpcServer's nodeIds is, so both are created like serverConnections - shipped unenforced, there
	//for an admin to enforce on the Resources page.  Read is all either offers:  it is all either gate asks.
	resources:{
		sessions:{ ops:["Read"] }, //opcSessions{…}, and the opcSessions/opcConnections/connectionStatus grafts on serverConnections (OpcSessionsQLAwait)
		search:{ ops:["Read"] }, //search(…) - the navbar's node search (SearchQLAwait)
		endpoint:{ ops:["Read"] } //endpoint{…} - the gateway's own OPC UA endpoint, its Endpoint tab and a group's served nodes (EndpointQLAwait)
	}
}
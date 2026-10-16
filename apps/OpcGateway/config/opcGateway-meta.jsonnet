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
{
	local tables = self.tables,
	tables:{
		server_connections:{
			columns: {
				server_connection_id: common.pkSequenced,
				is_default: common.types.bit+{i:101, default:false},
				default_browse_ns: common.types.uint16+{i:102, nullable:true},
				certificate_uri: common.types.varchar+{nullable:true, length:2048,i:103},
				url: common.types.varchar+{length:2048, i:104}
			}+common.slugColumns+{
				slug: common.slugColumns.slug+{updateable:false, comment:"The connection's identity everywhere but this table:  the url segment, the key the live UAClient sits under (UM), the sessions join key.  The update path skips a non-updateable column, so a rename is dropped rather than leaving the running gateway keyed by a name the row no longer has.  Only here - users/roles/groups may rename."}
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
			}+thresholds( 5 ),
			//The unique (connection, node) index.  No `deleted`:  a node leaves its group by purge, which frees the index for another
			//group, and rejoining is a new row and so a new node_index (spec *Collection*).
			naturalKeys: [["server_connection_id", "node"]]
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
		search:{ ops:["Read"] } //search(…) - the navbar's node search (SearchQLAwait)
	}
}
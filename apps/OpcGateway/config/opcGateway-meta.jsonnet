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
				security_mode: common.types.varchar+{ nullable:true, length:16, i:105, comment:"The message security mode pinned for the channel, UA's name:  None, Sign or SignAndEncrypt.  Null keeps the floor:  SignAndEncrypt with a certificate_uri, None without.  Sign or SignAndEncrypt needs a certificate_uri (OpcQLHook)." },
				security_policy: common.types.varchar+{ nullable:true, length:256, i:106, comment:"The security policy uri pinned for the channel, http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256;  one the gateway carries (ClientSecurity::SecuredPolicyUris), or None's.  Null takes the strongest the two sides share." },
				session_timeout: common.types.uint+{ nullable:true, i:107, comment:"The session timeout requested at CreateSession, in milliseconds as UA's Duration is (UA_ClientConfig::requestedSessionTimeout).  Null is open62541's default." },
				request_timeout: common.types.uint+{ nullable:true, i:108, comment:"How long a service call may take before the client fails it, in milliseconds (UA_ClientConfig::timeout), and the test connect's deadline.  Null is the gateway's 10 s." },
				ping_interval: common.types.uint+{ nullable:true, i:109, comment:"The keep-alive's period, in milliseconds (AsyncRequest::Ping).  Null is /gateway/pingInterval, or half of session_timeout where that is set alone." },
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
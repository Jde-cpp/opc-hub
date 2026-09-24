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
				//slug is the connection's identity everywhere but this table: the url segment, the key the live UAClient sits
				//under (UM), the sessions join key.  The update path skips a non-updateable column, so a rename is dropped rather
				//than leaving the running gateway keyed by a name the row no longer has.  Only here - users/roles/groups may rename.
				slug: common.slugColumns.slug+{updateable:false}
			},
			customInsertProc:true,
			naturalKeys: common.slugNKs
		},
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
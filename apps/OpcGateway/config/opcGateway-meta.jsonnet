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
//The OpcServer's address space is defined by the NodeSet2 xml files at /opcServer/configFiles, not by the database.
//The tables that used to persist it (node_ids, browse_names, objects, object_types, variables, variants, refs,
//data_types, servers, …) and the ServerConfigAwait chain that read them are gone;  git history has them if the
//DB-backed address space is ever revived.  The schema itself stays, for the one thing that outlived the tables:
//`nodeIds` is the access resource every node-level acl hangs off (OpcAuthorize::AssignRights matches
//resource.Slug=="nodeIds"; node-access.ts writes to it).  It used to exist only because the node_ids *table* had no
//`ops` key and so took DefaultOps, which is what made ResourceSyncAwait create the row - declared outright here now.
//`logs` is this instance's log, its log settings and the tag levels the AppServer stores for it (AppQL::TestLogs);  the
//AppServer reads its forwarded lines against it too, through the auth_resource the instance registers with.
{
	tables:{},
	resources:{
		//DB::DefaultOps - what the table carried - plus Subscribe.  `allowed` is what the permission table offers as
		//checkboxes (permission-table.ts `resource.availableRights`), and a node acl's whole point is a UA client that
		//reads, writes and *subscribes*:  without it the emulator's own Read|Update|Subscribe grant (Emulator.cpp
		//GrantWriteRights) was a mask the UI could not have produced.  It caps nothing server-side - ResourceLoadAwait
		//never reads the column - so this is purely what an operator is allowed to tick.
		nodeIds:{ ops:["Create","Read","Update","Delete","Purge","Administer","Subscribe"] },
		logs:{ ops:["Read","Update","Administer"] }
	}
}

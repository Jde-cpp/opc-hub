// What the hub's dev args share:  the three schemas' script dirs in the args' sqlType and the two seed dirs (args/mysql,
// args/sqlServer, args/sqlite), and the access + app + gateway mounts with the split apps' table prefixes (args/mysql,
// args/sqlServer - sqlite mounts through sqlite-common's helpers, with their own prefixes).  Each takes the merged args, for
// repoSourceDir and sqlType.  The installed args (args/install*) spell theirs over the product dir:  a deployment edits them.
{
	scriptPaths( args ):: [ args.repoSourceDir+"/"+dir+"/config/sql/"+args.sqlType for dir in ["libs/access", "apps/AppServer", "apps/OpcGateway"] ],
	dataPaths( args ):: [ args.repoSourceDir+"/apps/AppServer/config", args.repoSourceDir+"/libs/access/config" ],
	mounts( args ):: {
		access:{ meta: args.repoSourceDir+"/libs/access/config/access-meta.jsonnet", ql: args.repoSourceDir+"/libs/access/config/access-ql.jsonnet", prefix: "access_" },
		app:{ meta: args.repoSourceDir+"/apps/AppServer/config/app-meta.jsonnet", prefix: "app_" },
		gateway:{ meta: args.repoSourceDir+"/apps/OpcGateway/config/opcGateway-meta.jsonnet", prefix: "opc_" }
	}
}
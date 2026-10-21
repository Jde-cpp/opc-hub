// What the OpcServer's dev args share:  the instance name and the UA trust list (args/mysql, args/sqlServer, args/sqlite),
// and the access + opc mounts (args/mysql, args/sqlServer - sqlite mounts through sqlite-common's helpers).  Each takes the
// merged args, for sqlType, buildTarget, certsDir and repoSourceDir.  The installed args (args/install*) spell theirs over
// the product dir:  a deployment edits them.
{
	instanceName( args ):: "OpcServer."+args.sqlType+"."+args.buildTarget,
	//Production products only - never a test product's dir.  Every cert under these dirs opens a secured UA session,
	//and wherever this list is also the enrollment anchor (the test hosts, Jde.Opc.Hub) enrolls a user named by its
	//CN; the test binaries anchor their own dirs in their own configs (Opc.Server.Tests.jsonnet, Opc.Tests.jsonnet).
	trustedCertDirs( args ):: [
		args.certsDir( "OpcGateway" ),
		args.certsDir( "OpcHub" ), //Jde.Opc.Hub - the gateway role's OPC client certs live under its own product dir.
		args.certsDir( "PlcEmulator" ) //apps/OpcServer/emulator - its UA client cert.
	],
	//The access schema under `_appServer` (meta and ql:  the `_` prefix means no twins here), and the `opc` schema under
	//`opcSchema` - its meta declares only the nodeIds resource, so no scripts.
	mounts( args, opcSchema, accessPrefix=null ):: {
		_appServer:{
			access:{
				meta: args.repoSourceDir+"/libs/access/config/access-meta.jsonnet",
				ql: args.repoSourceDir+"/libs/access/config/access-ql.jsonnet",
				[if accessPrefix!=null then "prefix"]: accessPrefix
			}
		},
		[opcSchema]:{
			opc:{ meta: args.repoSourceDir+"/apps/OpcServer/config/opcServer-meta.jsonnet", prefix: "opc_" }
		}
	}
}
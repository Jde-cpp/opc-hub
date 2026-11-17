//The public demo's args (docs/demo/spec.md *The public origin*; apps/OpcHub/setup/demo lays this file and the drop-in that
//passes -include=args/demo).  args/install - edit THAT file for the OAuth client id, the host names, a certificate - plus what
//the box behind nginx changes:
//- the listen address, loopback as the user install's:  nginx on 443 is the one way in, and the admin's is an SSH tunnel.
//- the certificates that may log in, the five PLCs' and the seed's (setup/demo/seed.sh certs), in place of the OpcServer's,
//  whose unit the demo leaves disabled:  a dir that is never created is a warning in every log.
//- the seeds, a copy install-demo.sh makes of the package's without the bundled OPC UA server's three.  A start applies every
//  .mutation again, so the `OpcServer` connection the seed purged would be back at the next one.
(import '../install/args.libsonnet') + {
	local args = self,
	listenAddress: "127.0.0.1",
	access:{
		trustedCertDirs: [ args.certsDir("PlcEmulator.plc"+n) for n in std.range(1, 5) ] + [ args.certsDir("DemoSeed") ]
	},
	dbServers+:{ dataPaths: [ args.companyDir+"/OpcHub/sql-demo" ] }
}
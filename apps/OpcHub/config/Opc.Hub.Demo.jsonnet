// The public demo's hub (docs/demo/spec.md *Guests*, *The public origin*):  Opc.Hub.jsonnet under args/demo, plus what is no
// args key.  apps/OpcHub/setup/demo lays it beside that file, where the introspection paths resolve, and its drop-in runs
// `-settings=<this file> -include=args/demo -sync`.  The historian and OEE need nothing here:  the gateway's `hist` and `oee`
// blocks come with Opc.Hub.jsonnet.
local args = import 'args.libsonnet';
function( sync=false ) (import 'Opc.Hub.jsonnet')( sync ) + {
	access+:{ guest::: "guest" }, //a visitor's session acts as this user, which setup/demo/seed.sh makes, in the `visitor` role.
	gateway+:{
		//the five PLCs' channel certificates (apps/OpcServer/emulator/config/demo/demo.libsonnet), beside the base's:  no dir here is
		//among access.trustedCertDirs, the logins'.
		trustedCertDirs+: [ args.certsDir("PlcEmulator.plc"+n+".opc") for n in std.range(1, 5) ]
	},
	http+:{
		trustedProxies::: [ "127.0.0.1" ], //nginx, on this box:  a session is the visitor's address, not the front's.  `:::`, the base's key being hidden.
		timeout: "PT1H" //a session is minted for every call that names none:  an hour bounds how many the front's rate lets one address leave.
	}
}
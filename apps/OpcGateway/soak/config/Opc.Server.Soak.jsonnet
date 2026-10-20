// Soak overlay: production OpcServer config (DI+IA+IA-examples nodesets, trustedCertDirs incl. the gateway's certs
// dir) with sync=true baked in and log tags flattened for a 24h run.
// Launch with -include=../../../OpcServer/config/args/sqlite (relative to this file) and -arg path=<run>/db/opc.db.
local base = (import '../../../OpcServer/config/Opc.Server.jsonnet')(sync=true);
local soakLogging = import 'soak-logging.libsonnet';
base + {
	logging+: {
		spd+: {
			tags: soakLogging.ua + { io: "Warning", threads: "Warning" }
		}
	}
}
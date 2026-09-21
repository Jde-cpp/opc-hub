// Soak overlay: production AppServer config with sync=true baked in (fresh per-run sqlite file gets schema+seed)
// and log tags flattened - the production tag map has many Trace/Debug tags, unbounded over 24h (no log rotation).
// Launch with -include=../../../AppServer/config/args/sqlite (relative to this file), -arg path=<run>/db/app.db and
// -arg sessionTimeout=<duration> (soak.sh --session-timeout; P1D is the production value).
local base = (import '../../../AppServer/config/App.Server.jsonnet')(sync=true);
local sessionTimeout = std.extVar( 'sessionTimeout' );
base + {
	//How long a web session lives without being slid - the wall soak-findings #13 hit a day in.  Shortened, a run of
	//minutes crosses it several times over.  ':::' because the base hides socketTimeout, and ':' would inherit that.
	http+: { timeout: sessionTimeout, socketTimeout::: sessionTimeout },
	//the soak client logs in with its own web cert (main.cpp -> SslSettings), issued under http.ssl.productName "Opc.Soak" - a test-only
	//product, so it is anchored in this overlay rather than in the production trustedCertDirs list (see the comment there).
	access+: { trustedCertDirs+: [ "$(ProgramData)/Jde-Cpp/Opc.Soak/ssl/certs" ] },
	logging+: {
		spd+: {
			tags: {
				default: "Information"
			}
		}
	}
}

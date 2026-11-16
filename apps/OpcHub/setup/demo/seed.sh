#!/usr/bin/env bash
# The public demo's seed, its access half (docs/demo/plan.md Phase 8;  README.md beside this file is the runbook).  As root, on
# the box, against the loopback hub.  Each step is safe to run again.
#   seed.sh certs           the five PLCs' certificates and the seed's own pair - before the PLC units first start
#   seed.sh access          owner for the seed's user, and every right on every resource for `owner`;  the bundled OPC UA server's
#                           connection purged;  the guest in `visitor`;  the PLCs that have logged in in `demo-plc`, `--plcs <seconds>`
#                           waiting that long for their first logins;  and every resource enforced, last
#   seed.sh grant <email>   owner for the Google user of that address, enrolled by its first sign-in through the tunnel
#   seed.sh session         a session of the seed's user on stdout, for smoke.sh
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
data=${JDE_DATA:-/var/lib/Jde-Cpp}
overlays=/etc/jde-cpp/apps/OpcServer/emulator/config/demo
die(){ echo "seed.sh: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run it as root:  the PLCs' certificates are made as jde-cpp, and the seed's key is root's alone"

certs(){
	#As each PLC's unit would run it, so the pairs land where the unit reads them:  a console run's data root is ~/.config, a unit's
	#$STATE_DIRECTORY's parent (libs/fwk/src/process/os/linux/LinuxApp.cpp), and the unit's JDE_PASSCODE encrypts the keys.  A pair
	#that exists is kept, so each PLC stays the hub user its first login enrolled.
	local n out
	for n in 1 2 3 4 5; do
		out=$(systemd-run --quiet --wait --collect --pipe --uid=jde-cpp --gid=jde-cpp -p StateDirectory=Jde-Cpp -p StateDirectoryMode=0750 \
			-p EnvironmentFile=-/etc/jde-cpp/env -p WorkingDirectory="$here" \
			"$here/Jde.Opc.PlcEmulator" -c -settings="$overlays/plc$n.jsonnet" -include=../args/install -createCert 2>&1) \
			|| { echo "$out" >&2; die "plc$n:  -createCert failed"; }
		echo "seed.sh: plc$n:  $data/PlcEmulator.plc$n/ssl and PlcEmulator.plc$n.opc/ssl"
	done
	#The seed's own, in a sixth dir args/demo trusts.  The hub reads the certificate;  the key is root's, so nothing running as
	#jde-cpp can log in as the owner it becomes.
	local ssl=$data/DemoSeed/ssl
	if [ ! -f "$ssl/private/demo-seed.pem" ] || [ ! -f "$ssl/certs/demo-seed.pem" ]; then
		install -d -m 755 -o jde-cpp -g jde-cpp "$data/DemoSeed" "$ssl" "$ssl/certs"
		install -d -m 700 "$ssl/private"
		( umask 077; openssl req -x509 -newkey rsa:2048 -noenc -keyout "$ssl/private/demo-seed.pem" -out "$ssl/certs/demo-seed.pem" -days 3650 -subj "/CN=demo-seed" 2>/dev/null ) \
			|| die "openssl could not make the seed's pair under $ssl"
		chmod 644 "$ssl/certs/demo-seed.pem"
	fi
	echo "seed.sh: demo-seed:  $ssl"
}

case "${1:-}" in
	certs) [ $# = 1 ] || die "certs takes no argument"; certs;;
	access|grant|session) JDE_DATA=$data exec python3 "$here/seed.py" "$@";;
	*) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 2;;
esac
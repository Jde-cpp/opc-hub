#!/usr/bin/env bash
# The public demo's reset (docs/demo/plan.md Phase 9;  README.md beside this file is the runbook):  the plant from nothing.  Until
# the first release no database or historian file has to survive a change, so an upgrade that changes either's format takes this.
#   sudo /opt/jde-cpp/demo/reset.sh --yes
# In this order, so the 443 site never fronts a hub whose resources are not enforced, where every caller holds every right:
# the 443 site out;  the PLCs and the hub stopped;  the hub's database and the historian's directory removed;  the hub started,
# its -sync making the tables and applying the seeds;  the PLCs started, each enrolled again by its login;  the seed whole,
# `seed.sh certs`, `access` and `plant`;  and the 443 site back, last, where it was enabled.  History starts again.  The
# certificates, the settings and the logs stay.  Without --yes it says what it would remove, and removes nothing.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
data=${JDE_DATA:-/var/lib/Jde-Cpp}
db=$data/OpcHub/OpcHub.db #args/install's catalog, which args/demo keeps
hist=$data/OpcHub/hist/opc-gateway #Opc.Gateway.jsonnet's hist.path, under args/install's logsDir
site=/etc/nginx/sites-enabled/jde-demo
die(){ echo "reset.sh: $*" >&2; exit 1; }

if [ $# != 1 ] || [ "$1" != --yes ]; then
	echo "reset.sh removes the demo's database, $db, and its history, $hist:  every machine's figures start again."
	echo "  sudo $0 --yes"
	exit 2
fi
[ "$(id -u)" = 0 ] || die "run it as root"
[ -d "$data/OpcHub" ] || die "no $data/OpcHub - the jde-opchub package and install-demo.sh come first"
[ -x "$here/seed.sh" ] || die "no seed.sh beside this file - run the one install-demo.sh laid, /opt/jde-cpp/demo/reset.sh"

fronted=0; [ ! -L "$site" ] || fronted=1
rm -f "$site"
systemctl try-reload-or-restart nginx

systemctl stop jde-plc@{1,2,3,4,5}.service jde-opchub.service
rm -f "$db" "$db-wal" "$db-shm" "$db-journal"
rm -rf "$hist"
systemctl start jde-opchub.service
up=0
for _ in $(seq 60); do #a unit that fails its start restarts every 5 s and `systemctl start` says nothing of it
	if curl -s -o /dev/null --max-time 5 http://127.0.0.1:1967/serverSettings; then up=1; break; fi
	sleep 1
done
[ $up = 1 ] || die "the hub does not answer on 127.0.0.1:1967 over its new database - journalctl -u jde-opchub -e.  The 443 site stays out until this runs through"
"$here/seed.sh" certs #a pair that exists is kept
systemctl start jde-plc@{1,2,3,4,5}.service
"$here/seed.sh" access --plcs 60
"$here/seed.sh" plant

if [ $fronted = 1 ]; then
	ln -sfn /etc/jde-cpp/demo/nginx-demo.conf "$site"
	if ! out=$(nginx -t 2>&1); then
		rm -f "$site"
		die "nginx refused nginx-demo.conf, which was not enabled:  $out"
	fi
	systemctl reload-or-restart nginx
	echo "reset the demo:  the plant is seeded and the 443 site is back.  next:  sudo $here/smoke.sh --hub http://127.0.0.1:1967"
else
	echo "reset the demo:  the plant is seeded.  The 443 site was not enabled and is not now:  install-demo.sh enables it"
fi
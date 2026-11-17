#!/usr/bin/env bash
# The public demo's reset (docs/demo/plan.md Phase 9;  README.md beside this file is the runbook):  the plant from nothing.  Until
# the first release no database or historian file has to survive a change, so an upgrade that changes either's format takes this.
#   sudo /opt/jde-cpp/demo/reset.sh --yes
# In this order, so the 443 site never fronts a hub whose resources are not enforced, where every caller holds every right:
# the 443 site out;  the PLCs and the hub stopped;  the hub's database and the historian's directory removed;  the hub started,
# its -sync making the tables and applying the seeds;  the PLCs started, each enrolled again by its login;  the seed whole,
# `seed.sh certs` and `plant`, which runs the access half first;  and the 443 site back, last, where it was enabled.  History
# starts again.  The certificates, the settings and the logs stay.  Without --yes it says what it would remove, and removes nothing.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
data=${JDE_DATA:-/var/lib/Jde-Cpp}
db=$data/OpcHub/OpcHub.db #args/install's catalog, which args/demo keeps
hist=$data/OpcHub/hist/opc-gateway #Opc.Gateway.jsonnet's hist.path, under args/install's logsDir
site=/etc/nginx/sites-enabled/jde-demo
fronted=$data/OpcHub/reset-fronted #the 443 site was on when a reset began:  kept until it is back, so a run that failed leaves it to the next
die(){ echo "reset.sh: $*" >&2; exit 1; }

if [ $# != 1 ] || [ "$1" != --yes ]; then
	echo "reset.sh removes the demo's database, $db, and its history, $hist:  every machine's figures start again."
	echo "  sudo $0 --yes"
	exit 2
fi
[ "$(id -u)" = 0 ] || die "run it as root"
[ -d "$data/OpcHub" ] || die "no $data/OpcHub - the jde-opchub package and install-demo.sh come first"
[ -x "$here/seed.sh" ] && [ -f "$here/demo-lib.sh" ] || die "no seed.sh or demo-lib.sh beside this file - run the one install-demo.sh laid, /opt/jde-cpp/demo/reset.sh"
source "$here/demo-lib.sh"

[ ! -L "$site" ] || touch "$fronted"
rm -f "$site"
systemctl try-reload-or-restart nginx

systemctl stop jde-plc@{1,2,3,4,5}.service jde-opchub.service
rm -f "$db" "$db-wal" "$db-shm" "$db-journal"
rm -rf "$hist"
systemctl start jde-opchub.service
hubUp || die "the hub does not answer on 127.0.0.1:1967 over its new database - journalctl -u jde-opchub -e.  The 443 site stays out until this runs through"
"$here/seed.sh" certs #a pair that exists is kept
systemctl start jde-plc@{1,2,3,4,5}.service
"$here/seed.sh" plant

if [ -f "$fronted" ]; then
	site nginx-demo.conf jde-demo
	rm -f "$fronted"
	echo "reset the demo:  the plant is seeded and the 443 site is back.  next:  sudo $here/smoke.sh --hub http://127.0.0.1:1967"
else
	echo "reset the demo:  the plant is seeded.  The 443 site was not enabled and is not now:  install-demo.sh enables it"
fi
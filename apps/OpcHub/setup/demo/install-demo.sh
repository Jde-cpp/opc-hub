#!/usr/bin/env bash
# The public demo's install (docs/demo/plan.md Phases 8 to 10;  README.md beside this file is the runbook).  Run from the unpacked
# jde-opc-demo tarball, as root, on a box with this release's jde-opchub package, nginx, certbot and the smoke's Python,
# python3-websockets and python3-protobuf.  An upgrade is the same line from the newer tarball, after the newer package.
#   sudo ./install-demo.sh [--email <address>]
#     --email   the certbot account's address, for the first certificate on a box where certbot has no account yet
# In this order, so the 443 site never fronts a hub whose resources are not enforced, where every caller holds every right:
# the 443 site out, on an upgrade;  the files;  the port-80 site, and certbot's first certificate over it;  the hub on the demo's
# config;  `seed.sh certs`, the PLC units and `seed.sh plant`, which runs the access half first;  the retention's timer;  and
# the 443 site last.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
name=opchub.trypoply.com #the two nginx sites' server_name, the certificate's and the policy's
die(){ echo "install-demo.sh: $*" >&2; exit 1; }
[ -f "$here/opt/jde-cpp/demo/demo-lib.sh" ] || die "run it from the unpacked tarball (opt/jde-cpp/demo/demo-lib.sh not found beside it)"
source "$here/opt/jde-cpp/demo/demo-lib.sh"

email=
while [ $# -gt 0 ]; do
	case "$1" in
		--email) email=${2:?}; shift 2;;   --email=*) email=${1#*=}; shift;;
		-h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		*) die "unknown option '$1'";;
	esac
done

[ "$(id -u)" = 0 ] || die "run it as root"
[ -x "$here/opt/jde-cpp/demo/Jde.Opc.PlcEmulator" ] || die "run it from the unpacked tarball (opt/jde-cpp/demo/Jde.Opc.PlcEmulator not found beside it)"
[ -x /opt/jde-cpp/opchub/Jde.Opc.Hub ] || die "the jde-opchub package is not installed - apt install ./jde-opchub_<version>_amd64.deb first"
#The demo's hub config extends the package's, and its seeds are a copy of the package's:  the two are one release's.
package=$(dpkg-query -W -f '${Version}' jde-opchub)
[ "$package" = "$(cat "$here/VERSION")" ] || die "this tarball goes with jde-opchub $(cat "$here/VERSION") and the installed one is $package - take both from one release"
for t in nginx certbot openssl python3 curl; do command -v $t >/dev/null || die "$t not found - apt install nginx certbot openssl python3 curl"; done
python3 -c 'import websockets, google.protobuf' 2>/dev/null || die "the smoke's socket half has no websockets or protobuf - apt install python3-websockets python3-protobuf"
[ -d /etc/nginx/sites-enabled ] || die "no /etc/nginx/sites-enabled - the sites are laid for Ubuntu's nginx package"
missing=$(for f in "$here"/opt/jde-cpp/demo/*; do { ldd "$f" 2>/dev/null || true; } | awk '/ => not found/{print $1}'; done | sort -u | paste -sd' ')
[ -z "$missing" ] || die "the PLC emulator loads $missing, which neither this tarball nor the system has - nothing was installed"

#An upgrade:  the 443 site out before anything changes.  The upgraded hub may hold tables nothing enforces yet.
rm -f /etc/nginx/sites-enabled/jde-demo
systemctl try-reload-or-restart nginx

#--- files -------------------------------------------------------------------------------------------------------------
rm -rf /opt/jde-cpp/demo #the tarball's own dir:  a file an older one shipped does not linger, and a running PLC keeps its unlinked image until its restart below
cp -r "$here/opt/jde-cpp/demo" /opt/jde-cpp/
#Under /etc an operator's edit is kept, as dpkg keeps a conffile's:  a PLC's figures, the banner, nginx's limits and its policy's
#header are tuned on the box.  .dist holds what the last run laid, so a file that differs from it and from this release's is
#the operator's:  it stays in use, this release's goes beside it as .new, and the closing message names it.
dist=/etc/jde-cpp/demo/.dist
kept=()
while IFS= read -r -d '' f; do
	mode=$(stat -c %a "$here/etc/$f")
	rm -f "/etc/$f.new"
	if [ -f "/etc/$f" ] && ! cmp -s "/etc/$f" "$here/etc/$f" && ! cmp -s "/etc/$f" "$dist/$f"; then
		install -m 644 "$here/etc/$f" "/etc/$f.new" #never executable:  certbot runs every executable in its hooks dir
		kept+=( "/etc/$f" )
	else
		install -D -m "$mode" "$here/etc/$f" "/etc/$f"
	fi
	install -D -m 644 "$here/etc/$f" "$dist/$f"
done < <(cd "$here/etc" && find . -type f -printf '%P\0')
install -d -o jde-cpp -g jde-cpp /var/lib/Jde-Cpp/PlcEmulator /var/lib/Jde-Cpp/PlcEmulator/nodesets
install -m 644 -o jde-cpp -g jde-cpp -t /var/lib/Jde-Cpp/PlcEmulator/nodesets "$here"/var/lib/Jde-Cpp/PlcEmulator/nodesets/*
#The hub's seeds under args/demo, the package's without the bundled OPC UA server's:  its connection, that connection's
#provider and its role.  A start applies every .mutation it finds, so the package's own dir would bring the connection back
#after the seed purged it.  Recreated from the package's on each run, so an upgrade's seeds follow.
rm -rf /var/lib/Jde-Cpp/OpcHub/sql-demo
install -d -o jde-cpp -g jde-cpp /var/lib/Jde-Cpp/OpcHub/sql-demo
for f in /var/lib/Jde-Cpp/OpcHub/sql/*.mutation /var/lib/Jde-Cpp/OpcHub/sql/*.roles; do
	case "$f" in *_opcServer.*) ;; *) install -m 644 -o jde-cpp -g jde-cpp "$f" /var/lib/Jde-Cpp/OpcHub/sql-demo/;; esac
done

#--- port 80 and the certificate ---------------------------------------------------------------------------------------
#nginx refuses a config naming a certificate that is not there, so the 443 site waits for certbot, and certbot's challenge
#is answered by the port-80 site, which serves nothing else.  A certificate already at certbot's path - an upgrade, or one laid
#by hand - is used as found.
install -d /var/www/certbot
site nginx-demo-80.conf jde-demo-80
if [ ! -f "/etc/letsencrypt/live/$name/fullchain.pem" ]; then
	certbot certonly --webroot -w /var/www/certbot -d "$name" --non-interactive ${email:+--agree-tos -m "$email"} \
		|| die "certbot issued no certificate for $name - its A and AAAA records reach this box, and port 80 is open?  Then run this again"
fi

#--- the hub, the PLCs and the seed ------------------------------------------------------------------------------------
systemctl daemon-reload
systemctl restart jde-opchub.service #onto the drop-in:  Opc.Hub.Demo.jsonnet under args/demo
hubUp || die "the hub does not answer on 127.0.0.1:1967 under the demo's config - journalctl -u jde-opchub -e;  \`systemctl revert jde-opchub\` and a restart put the package's unit back"
/opt/jde-cpp/demo/seed.sh certs #before a PLC's first start:  it logs in with its certificate
systemctl enable --quiet jde-plc@{1,2,3,4,5}.service
systemctl restart jde-plc@{1,2,3,4,5}.service #an upgrade's, onto the new files
#the access half, each PLC's user enrolled by its first login and put in `demo-plc` to run enforced, waiting 60 s for the five;
#then the connections, the machines, the lines and the floor, where there are none
/opt/jde-cpp/demo/seed.sh plant
systemctl enable --quiet --now jde-demo-retention.timer #daily:  the historian's day directories older than 90 days

#--- 443 ---------------------------------------------------------------------------------------------------------------
site nginx-demo.conf jde-demo

cat <<MSG
installed the demo at https://$name:
  the hub      jde-opchub on Opc.Hub.Demo.jsonnet, loopback 1967 behind nginx - journalctl -u jde-opchub
  the PLCs     jde-plc@1 to jde-plc@5 - journalctl -u 'jde-plc@*'
  the plant    plc1 to plc5 on opc.tcp 4842 to 4846, five machines, the site and its two lines - /opt/jde-cpp/demo/seed.sh plant
  retention    jde-demo-retention.timer, daily:  the history's days older than 90 - journalctl -u jde-demo-retention
  settings     /etc/jde-cpp/demo/config.json, the nginx sites beside it, /etc/jde-cpp/apps/OpcServer/emulator/config/demo/plc<N>.jsonnet
next:
  sudo /opt/jde-cpp/demo/smoke.sh --hub http://127.0.0.1:1967
and the first time, the admin's sign-in through the tunnel, then \`seed.sh grant <email>\` (README.md, The first sign-in).
An upgrade whose release changed the database or the historian's files:  sudo /opt/jde-cpp/demo/reset.sh --yes (README.md, Reset).
MSG
for f in "${kept[@]}"; do echo "kept your edited $f - this release's is beside it as $(basename "$f").new: merge any change by hand"; done
/opt/jde-cpp/demo/highcharts.sh --check || true #the charts' add-on, which the site's owner lays:  there, missing, or another release's
#nginx's access log holds each visitor's address, and the demo keeps it 14 days:  the nginx package's own logrotate file does
#that, so it is checked here and not replaced.
rotate=/etc/logrotate.d/nginx
if ! grep -qE '^[[:space:]]*daily\b' $rotate 2>/dev/null || ! grep -qE '^[[:space:]]*rotate[[:space:]]+14\b' $rotate 2>/dev/null; then
	echo "WARNING: $rotate does not say \`daily\` and \`rotate 14\`:  nginx's access log is to be kept 14 days (README.md, Operating)"
elif ! systemctl is-enabled --quiet logrotate.timer 2>/dev/null; then
	echo "WARNING: logrotate.timer is not enabled, so nothing rotates nginx's access log:  apt install logrotate (README.md, Operating)"
fi
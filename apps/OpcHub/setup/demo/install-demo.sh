#!/usr/bin/env bash
# The public demo's install (docs/demo/plan.md Phase 8;  README.md beside this file is the runbook).  Run from the unpacked
# jde-opc-demo tarball, as root, on a box with this release's jde-opchub package, nginx and certbot.  An upgrade is the same
# line from the newer tarball, after the newer package.
#   sudo ./install-demo.sh [--email <address>]
#     --email   the certbot account's address, for the first certificate on a box where certbot has no account yet
# In this order, so the 443 site never fronts a hub whose resources are not enforced, where every caller holds every right:
# the 443 site out, on an upgrade;  the files;  the port-80 site, and certbot's first certificate over it;  the hub on the demo's
# config;  `seed.sh certs`, the PLC units and `seed.sh access`;  and the 443 site last.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
name=opchub.trypoply.com #the two nginx sites' server_name, the certificate's and the policy's
die(){ echo "install-demo.sh: $*" >&2; exit 1; }

email=
while [ $# -gt 0 ]; do
	case "$1" in
		--email) email=${2:?}; shift 2;;   --email=*) email=${1#*=}; shift;;
		-h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
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
site(){ #file link - enabled if nginx takes it, else taken back out so the next reload of nginx still works
	local out
	ln -sfn "/etc/jde-cpp/demo/$1" "/etc/nginx/sites-enabled/$2"
	if ! out=$(nginx -t 2>&1); then
		rm -f "/etc/nginx/sites-enabled/$2"
		die "nginx refused $1, which was not enabled:  $out"
	fi
	systemctl reload-or-restart nginx
}
install -d /var/www/certbot
site nginx-demo-80.conf jde-demo-80
if [ ! -f "/etc/letsencrypt/live/$name/fullchain.pem" ]; then
	certbot certonly --webroot -w /var/www/certbot -d "$name" --non-interactive ${email:+--agree-tos -m "$email"} \
		|| die "certbot issued no certificate for $name - its A and AAAA records reach this box, and port 80 is open?  Then run this again"
fi

#--- the hub, the PLCs and the seed ------------------------------------------------------------------------------------
systemctl daemon-reload
systemctl restart jde-opchub.service #onto the drop-in:  Opc.Hub.Demo.jsonnet under args/demo
up=0
for _ in $(seq 60); do #a unit that fails its start restarts every 5 s and `systemctl restart` says nothing of it
	if curl -s -o /dev/null --max-time 5 http://127.0.0.1:1967/serverSettings; then up=1; break; fi
	sleep 1
done
[ $up = 1 ] || die "the hub does not answer on 127.0.0.1:1967 under the demo's config - journalctl -u jde-opchub -e;  \`systemctl revert jde-opchub\` and a restart put the package's unit back"
/opt/jde-cpp/demo/seed.sh certs #before a PLC's first start:  it logs in with its certificate
systemctl enable --quiet jde-plc@{1,2,3,4,5}.service
systemctl restart jde-plc@{1,2,3,4,5}.service #an upgrade's, onto the new files
/opt/jde-cpp/demo/seed.sh access --plcs 60 #each PLC's user is enrolled by its first login, and takes `demo-plc` to run enforced

#--- 443 ---------------------------------------------------------------------------------------------------------------
site nginx-demo.conf jde-demo

cat <<MSG
installed the demo at https://$name:
  the hub      jde-opchub on Opc.Hub.Demo.jsonnet, loopback 1967 behind nginx - journalctl -u jde-opchub
  the PLCs     jde-plc@1 to jde-plc@5 - journalctl -u 'jde-plc@*'
  settings     /etc/jde-cpp/demo/config.json, the nginx sites beside it, /etc/jde-cpp/apps/OpcServer/emulator/config/demo/plc<N>.jsonnet
next:
  sudo /opt/jde-cpp/demo/smoke.sh --hub http://127.0.0.1:1967
and the first time, the admin's sign-in through the tunnel, then \`seed.sh grant <email>\` (README.md, The first sign-in).
MSG
for f in "${kept[@]}"; do echo "kept your edited $f - this release's is beside it as $(basename "$f").new: merge any change by hand"; done
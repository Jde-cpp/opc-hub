#!/usr/bin/env bash
# Jde OpcHub - the per-user install from the tarball, the counterpart of the Windows installer's "Current user" mode:
# no root, the products run as systemd --user units under this account.  The system-wide install is the .deb (README.md).
#   ./install.sh                 install or upgrade the hub and the Web UI files; enable and (re)start jde-opchub
#   ./install.sh --opcserver     ... and the OPC UA server
#   ./install.sh --uninstall     stop and disable the units, remove the programs and the settings - the data stays
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
configHome=${XDG_CONFIG_HOME:-$HOME/.config}
programs=${XDG_DATA_HOME:-$HOME/.local/share}/jde-cpp #the exes and their libs, the Web UI - %LOCALAPPDATA%\Programs\Jde-Cpp
dataRoot=$configHome/Jde-Cpp                          #Process::ProgramDataFolder() of a user process: the .db, ssl/, the logs, per product
config=$dataRoot/config                               #the settings mirror - %ProgramData%\Jde-Cpp\config
units=$configHome/systemd/user

opcServer=0; uninstall=0
for a in "$@"; do
	case "$a" in
		--opcserver) opcServer=1;;
		--uninstall) uninstall=1;;
		-h|--help) sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		*) echo "install.sh: unknown option '$a'" >&2; exit 2;;
	esac
done
command -v systemctl >/dev/null || { echo "install.sh: systemctl not found - the products run as systemd --user units" >&2; exit 1; }

if [ $uninstall = 1 ]; then
	systemctl --user disable --now jde-opcserver.service jde-opchub.service 2>/dev/null || true
	rm -f "$units/jde-opchub.service" "$units/jde-opcserver.service"
	systemctl --user daemon-reload || true
	rm -rf "$programs" "$config"
	#what the installer put in the product dirs goes; what the products created (the .db, ssl/, the logs) stays
	for p in OpcHub OpcServer; do
		rm -rf "$dataRoot/$p/sql" "$dataRoot/$p/nodesets"
		rm -f "$dataRoot/$p"/*.jsonnet "$dataRoot/$p/common-meta.libsonnet"
		rmdir "$dataRoot/$p" 2>/dev/null || true
	done
	rmdir "$dataRoot" 2>/dev/null || echo "kept $dataRoot (databases, certificates, logs) - delete it by hand for a clean slate"
	echo "uninstalled"
	exit 0
fi

[ -x "$here/opt/jde-cpp/opchub/Jde.Opc.Hub" ] || { echo "install.sh: run it from the unpacked tarball (opt/jde-cpp/opchub/Jde.Opc.Hub not found beside it)" >&2; exit 1; }
#Every library the products load must resolve, beside the exes or on the system:  the tarball carries those a system may
#lack (build-deb.sh), and a gap would otherwise show only as units restarting every 5 s on a loader error after this script
#said "installed" (reviews/install-issues.md #59).  Checked before anything is copied, so an upgrade leaves the running ones be.
missing=$(for f in "$here"/opt/jde-cpp/opchub/* "$here"/opt/jde-cpp/opcserver/*; do { ldd "$f" 2>/dev/null || true; } | awk '/ => not found/{print $1}'; done | sort -u | paste -sd' ')
if [ -n "$missing" ]; then
	echo "install.sh: the products load $missing, which neither this tarball nor the system has - nothing was installed.  Install the distro package that provides it, or use the .deb, whose apt install brings it." >&2
	exit 1
fi
install -d "$programs" "$config" "$dataRoot" "$units"
cp -r --remove-destination "$here/opt/jde-cpp/." "$programs/" #unlink first: a rerun copies over the running products, where a plain cp dies ETXTBSY on the exe and rewrites each mapped .so in place; a new inode, like dpkg's rename, leaves the old image to the restart below (reviews/m4-closing.md #1)
#The four overlays the README sends the operator to edit keep an edited copy, as the .deb's conffiles do:  $config/.dist
#holds what the last run laid, so one that differs from it - or, with no record (a tarball before this one), from this
#release's - is the operator's.  It stays in use, this release's lands beside it as .new, and the closing message names it
#(reviews/m4-closing.md #8).  The rest of $config is replaced:  the base configs and the metas must match the exes.
overlays="apps/OpcHub/config/args/install/args.libsonnet apps/OpcHub/config/args/install-user/args.libsonnet apps/OpcServer/config/args/install/args.libsonnet apps/OpcServer/config/args/install-user/args.libsonnet"
kept=""; unrecorded=""
for f in $overlays; do
	[ -f "$here/etc/jde-cpp/$f" ] || continue #a tarball without it has nothing to lay over it
	rm -f "$config/$f.new"
	if [ -f "$config/$f" ] && ! cmp -s "$config/$f" "$here/etc/jde-cpp/$f" && ! cmp -s "$config/$f" "$config/.dist/$f"; then
		mv "$config/$f" "$config/$f.kept"; kept="$kept $f"
		[ -f "$config/.dist/$f" ] || unrecorded="$unrecorded $f"
	fi
done
cp -r "$here/etc/jde-cpp/apps" "$here/etc/jde-cpp/libs" "$config/"
for f in $kept; do
	mv "$config/$f" "$config/$f.new"; mv "$config/$f.kept" "$config/$f"
done
for f in $overlays; do [ ! -f "$here/etc/jde-cpp/$f" ] || install -D -m 644 "$here/etc/jde-cpp/$f" "$config/.dist/$f"; done
[ -f "$dataRoot/env" ] || install -m 600 "$here/etc/jde-cpp/env" "$dataRoot/env" #the passcode file - never overwritten
for p in OpcHub OpcServer; do #sql/ and nodesets/ are installer-owned - recreated, so a seed an older version shipped cannot linger
	rm -rf "$dataRoot/$p/sql" "$dataRoot/$p/nodesets"
	install -d "$dataRoot/$p"
	cp -r "$here/var/lib/Jde-Cpp/$p/." "$dataRoot/$p/"
done
if [ $opcServer = 0 ]; then #the OPC UA server's seeds - the Web UI's Google provider, the server as the hub's default connection - go only with it (README.md "First login")
	rm -f "$dataRoot/OpcHub/sql/access_google.mutation" "$dataRoot/OpcHub/sql/access_opcServer.mutation" "$dataRoot/OpcHub/sql/gateway_opcServer.mutation" "$dataRoot/OpcHub/sql/access_opcServer.roles"
fi
chmod 700 "$dataRoot"

#The --user counterparts of the package's units:  no User=/StateDirectory= (the data root is this account's $XDG_CONFIG_HOME,
#pinned here so the manager's environment cannot move it), WantedBy=default.target.  `-c`: foreground, log on the journal.
#`-include=args/install-user`, where the package's units take args/install:  that overlay binds the listeners to 127.0.0.1,
#so an install that needed no root does not publish 1967, 1970 and 4840 to the network it cannot open a firewall port on
#(reviews/install-issues.md #32; the Windows current-user mode is the same overlay, #16).  README.md, "Per-user install",
#says how to reach it from another machine.
unit(){ #name description exeDir exe settings
	cat > "$units/$1.service" <<UNIT
# Jde $2 - installed by jde-opchub's install.sh (per-user).  \`systemctl --user edit $1\` for local changes.
[Unit]
Description=Jde $2
Documentation=https://github.com/Jde-cpp/opc-hub
$6
[Service]
Type=simple
Environment=XDG_CONFIG_HOME=$configHome
EnvironmentFile=-$dataRoot/env
WorkingDirectory=$programs/$3
ExecStart=$programs/$3/$4 -c -settings=$config/$5 -include=args/install-user -sync
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
UNIT
}
unit jde-opchub "OpcHub - AppServer + OPC gateway (port 1967)" opchub Jde.Opc.Hub apps/OpcHub/config/Opc.Hub.jsonnet ""
unit jde-opcserver "OpcServer - OPC UA server (opc.tcp 4840, http 1970)" opcserver Jde.Opc.Server apps/OpcServer/config/Opc.Server.Install.jsonnet \
	"$(printf 'Requires=jde-opchub.service\nAfter=jde-opchub.service')"
systemctl --user daemon-reload
services=jde-opchub.service
[ $opcServer = 0 ] || services="$services jde-opcserver.service"
systemctl --user enable $services
#restart, not start: an upgrade over a running instance.  One call, as postinst's try-restart:  jde-opcserver Requires= the hub,
#so the hub's restart already restarts a running server, and a second call would stop that new copy mid-start (install-issues #58).
systemctl --user restart $services
cat <<MSG
installed for $USER:
  programs  $programs
  settings  $config  (replaced on a rerun - but for an args.libsonnet you edited, kept)
  data      $dataRoot/<Product>  (the sqlite database is created on the first start)
  units     $units/jde-opchub.service, jde-opcserver.service$([ $opcServer = 1 ] || echo " (not enabled: systemctl --user enable --now jde-opcserver)")
  log       journalctl --user -u jde-opchub
The units run while you are logged in; \`loginctl enable-linger $USER\` starts them at boot instead.
MSG
for f in $kept; do
	case " $unrecorded " in
		*" $f "*) echo "kept $config/$f - it differs from this release's and no earlier run recorded what it laid; if you never edited it, move the $(basename "$f").new beside it over it";;
		*) echo "kept your edited $config/$f - this release's is beside it as $(basename "$f").new: merge any change by hand";;
	esac
done

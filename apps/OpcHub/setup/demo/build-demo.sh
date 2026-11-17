#!/usr/bin/env bash
# The public demo's box (docs/demo/plan.md Phases 8 and 9).  Builds jde-opc-demo-<version>.tar.gz from the release build tree, beside
# the .deb ../linux/build-deb.sh wrote there, whose version it takes:  the demo's hub config extends the package's, so the two
# deploy as a pair.  The .deb carries none of this - no customer install has the demo.  README.md beside this file is the
# runbook the tarball ships;  the tarball mirrors the box's root, which install-demo.sh lays:
#   opt/jde-cpp/demo                        Jde.Opc.PlcEmulator with the .so's it loads, seed.sh, seed.py, floor.json, reset.sh, smoke.sh, the runbook
#   etc/jde-cpp/apps/OpcHub/config          Opc.Hub.Demo.jsonnet and args/demo, beside the package's Opc.Hub.jsonnet
#   etc/jde-cpp/apps/OpcServer/emulator     the emulator's installed config, its args and the five PLCs' overlays, the repo's layout
#   etc/jde-cpp/demo                        the site's config.json and the two nginx sites
#   etc/systemd/system                      jde-plc@.service and the hub unit's drop-in
#   etc/letsencrypt/renewal-hooks/deploy    certbot's hook, which reloads nginx
#   var/lib/Jde-Cpp/PlcEmulator/nodesets    demo.NodeSet2.xml
set -euo pipefail

usage(){ cat <<'USAGE'
usage: build-demo.sh [options]
  --build-dir <dir>    the release build tree, with Jde.Opc.PlcEmulator built   default: as build-deb.sh's
  --out-dir <dir>      where build-deb.sh wrote the .deb; the tarball goes beside it   default: <build dir>/setup
  --version <v>        default: the version of the one jde-opchub_*.deb in --out-dir
  --no-strip           keep the debug sections
  --patchelf <path>    default: the patchelf on PATH
  -h, --help
USAGE
}
die(){ echo "build-demo.sh: $*" >&2; exit 1; }

buildDir=; outDir=; version=; strip=1; patchelf=patchelf
while [ $# -gt 0 ]; do
	case "$1" in
		--build-dir) buildDir=${2:?}; shift 2;;   --build-dir=*) buildDir=${1#*=}; shift;;
		--out-dir) outDir=${2:?}; shift 2;;       --out-dir=*) outDir=${1#*=}; shift;;
		--version) version=${2:?}; shift 2;;      --version=*) version=${1#*=}; shift;;
		--no-strip) strip=0; shift;;
		--patchelf) patchelf=${2:?}; shift 2;;    --patchelf=*) patchelf=${1#*=}; shift;;
		-h|--help) usage; exit 0;;
		*) usage >&2; die "unknown option '$1'";;
	esac
done

demoDir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
linuxDir=$(dirname "$demoDir")/linux
repo=$(cd "$demoDir/../../../.." && pwd) #apps/OpcHub/setup/demo -> the repo root
[ -n "$buildDir" ] || buildDir="${JDE_BUILD_DIR:-/mnt/ram/linux}/${JDE_COMPILER:-clang++}/$(basename "$repo")/release"
[ -n "$outDir" ] || outDir="$buildDir/setup"
emulator=$buildDir/apps/OpcServer/emulator/Jde.Opc.PlcEmulator
[ -f "$emulator" ] || die "missing $emulator - build Jde.Opc.PlcEmulator in the release tree first, or pass --build-dir"
for t in dpkg dpkg-deb ldd; do command -v $t >/dev/null || die "$t not found (dpkg, binutils)"; done
[ $strip = 0 ] || command -v strip >/dev/null || die "strip not found (binutils) - or --no-strip"
command -v "$patchelf" >/dev/null || die "patchelf not found - apt install patchelf, or pip install patchelf and --patchelf <path>"
arch=$(dpkg --print-architecture)
if [ -z "$version" ]; then
	debs=( "$outDir"/jde-opchub_*_"$arch".deb )
	[ -f "${debs[0]}" ] || die "no jde-opchub_*_$arch.deb in $outDir - run build-deb.sh first, or pass --version"
	[ ${#debs[@]} = 1 ] || die "${#debs[@]} jde-opchub packages in $outDir - pass the --version this tarball goes with"
	version=$(dpkg-deb -f "${debs[0]}" Version)
fi

stage=$(mktemp -d "$outDir/demo-stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT
optDir=$stage/opt/jde-cpp/demo; etcDir=$stage/etc/jde-cpp

#the emulator, with what it loads from outside the system and what a system may lack, as the package's tarball has its
#products (package-lib.sh)
source "$linuxDir/package-lib.sh"
install -D -m 755 "$emulator" "$optDir/Jde.Opc.PlcEmulator"
bundle "$optDir" "$emulator"
if [ $strip = 1 ]; then
	for f in "$optDir"/*; do strip --strip-debug "$f"; done
fi
carry "$stage" "$optDir"
install -m 755 -t "$optDir" "$demoDir/seed.sh" "$demoDir/seed.py" "$demoDir/reset.sh" "$demoDir/smoke.sh"
install -m 644 "$demoDir/floor.json" "$optDir/floor.json" #the plant's floor, which `seed.sh plant` reads
install -m 644 "$demoDir/README.md" "$optDir/README.md" #the runbook stays on the box

#the settings mirror, in the repo's layout as the package's is, so each file's relative imports resolve:  the hub's demo config
#beside Opc.Hub.jsonnet, and the emulator's chain, which reaches the package's libs/db/config/paths-common.libsonnet
for f in apps/OpcHub/config/Opc.Hub.Demo.jsonnet apps/OpcHub/config/args/demo/args.libsonnet \
	apps/OpcServer/emulator/config/Opc.PlcEmulator.jsonnet apps/OpcServer/emulator/config/Opc.PlcEmulator.Hub.jsonnet \
	apps/OpcServer/emulator/config/Opc.PlcEmulator.Install.jsonnet apps/OpcServer/emulator/config/args/install/args.libsonnet \
	apps/OpcServer/emulator/config/demo/demo.libsonnet apps/OpcServer/emulator/config/demo/plc{1,2,3,4,5}.jsonnet; do
	[ -f "$repo/$f" ] || die "missing $repo/$f"
	install -D -m 644 "$repo/$f" "$etcDir/$f"
done
install -D -m 644 -t "$etcDir/demo" "$demoDir/config.json" "$linuxDir/nginx-demo.conf" "$linuxDir/nginx-demo-80.conf"
install -D -m 644 "$demoDir/jde-plc@.service" "$stage/etc/systemd/system/jde-plc@.service"
install -D -m 644 "$demoDir/demo.conf" "$stage/etc/systemd/system/jde-opchub.service.d/demo.conf"
install -D -m 755 "$demoDir/certbot-deploy-hook.sh" "$stage/etc/letsencrypt/renewal-hooks/deploy/jde-demo"
install -D -m 644 "$repo/apps/OpcServer/config/nodesets/demo.NodeSet2.xml" "$stage/var/lib/Jde-Cpp/PlcEmulator/nodesets/demo.NodeSet2.xml"
install -m 755 "$demoDir/install-demo.sh" "$stage/install-demo.sh"
install -m 644 "$demoDir/README.md" "$repo/LICENSE" "$repo/THIRD-PARTY-NOTICES.txt" "$stage/"
printf '%s' "$version" > "$stage/VERSION" #the package this tarball goes with:  install-demo.sh refuses another's
chmod 644 "$stage/VERSION"
find "$stage" -type d -exec chmod 755 {} +

#one top-level directory, the archive's own name, as the package's tarball
tarRoot=jde-opc-demo-$version
tarFile=$outDir/$tarRoot.tar.gz
tar -czf "$tarFile" --owner=0 --group=0 --numeric-owner -C "$stage" --transform "s,^\./,$tarRoot/,S" \
	./opt ./etc ./var ./install-demo.sh ./README.md ./LICENSE ./THIRD-PARTY-NOTICES.txt ./VERSION
echo "built $tarFile ($(du -h "$tarFile" | cut -f1)) - unpacks into $tarRoot/"
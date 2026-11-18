#!/usr/bin/env bash
# The public demo's box (docs/demo/plan.md Phases 8 to 10).  Builds jde-opc-demo-<version>.tar.gz from the release build tree, beside
# the .deb ../linux/build-deb.sh wrote there, whose version it takes:  the demo's hub config extends the package's, so the two
# deploy as a pair.  The .deb carries none of this - no customer install has the demo.  README.md beside this file is the
# runbook the tarball ships;  the tarball mirrors the box's root, which install-demo.sh lays:
#   opt/jde-cpp/demo                        Jde.Opc.PlcEmulator with the .so's it loads, seed.sh, seed.py, floor.json, reset.sh, retention.sh, demo-lib.sh, the
#                                           runbook, the smoke:  smoke.sh, smoke.py and proto/, its Python modules of the gateway's socket, and
#                                           highcharts.sh, which fetches the charts' add-on on the box:  none of Highcharts is in the tarball
#   etc/jde-cpp/apps/OpcHub/config          Opc.Hub.Demo.jsonnet and args/demo, beside the package's Opc.Hub.jsonnet
#   etc/jde-cpp/apps/OpcServer/emulator     the emulator's installed config, its args and the five PLCs' overlays, the repo's layout
#   etc/jde-cpp/demo                        the site's config.json and the two nginx sites
#   etc/systemd/system                      jde-plc@.service, the hub unit's drop-in, and the retention's service and timer
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
  --protoc <path>      apt's protoc, 3.21, for smoke.py's modules   default: /usr/bin/protoc
  --proto-include <dir>   where its google/protobuf/*.proto are, libprotobuf-dev's   default: <protoc's prefix>/include
  -h, --help
USAGE
}
die(){ echo "build-demo.sh: $*" >&2; exit 1; }

buildDir=; outDir=; version=; strip=1; patchelf=patchelf; protoc=/usr/bin/protoc; protoInclude=
while [ $# -gt 0 ]; do
	case "$1" in
		--build-dir) buildDir=${2:?}; shift 2;;   --build-dir=*) buildDir=${1#*=}; shift;;
		--out-dir) outDir=${2:?}; shift 2;;       --out-dir=*) outDir=${1#*=}; shift;;
		--version) version=${2:?}; shift 2;;      --version=*) version=${1#*=}; shift;;
		--no-strip) strip=0; shift;;
		--patchelf) patchelf=${2:?}; shift 2;;    --patchelf=*) patchelf=${1#*=}; shift;;
		--protoc) protoc=${2:?}; shift 2;;        --protoc=*) protoc=${1#*=}; shift;;
		--proto-include) protoInclude=${2:?}; shift 2;;   --proto-include=*) protoInclude=${1#*=}; shift;;
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
#smoke.py runs on apt's python3-protobuf, 3.21 on the demo's box and on the watch's runner, which reads the code of a protoc
#of 3.20 or 3.21 and not a later one's:  the superbuild's protoc is no use here.
command -v "$protoc" >/dev/null || die "$protoc not found - apt install protobuf-compiler, or pass --protoc"
protocVersion=$("$protoc" --version)
case "$protocVersion" in "libprotoc 3.20."*|"libprotoc 3.21."*) ;; *) die "$protoc is $protocVersion:  smoke.py's modules take apt's, 3.21 - apt install protobuf-compiler, or pass --protoc";; esac
[ -n "$protoInclude" ] || protoInclude=$(dirname "$(dirname "$(command -v "$protoc")")")/include
[ -f "$protoInclude/google/protobuf/timestamp.proto" ] || die "no google/protobuf/timestamp.proto under $protoInclude - apt install libprotobuf-dev, or pass --proto-include"
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
install -m 755 -t "$optDir" "$demoDir/seed.sh" "$demoDir/seed.py" "$demoDir/reset.sh" "$demoDir/smoke.sh" "$demoDir/smoke.py" "$demoDir/retention.sh" "$demoDir/highcharts.sh"
#highcharts.sh lays the version the site is written against, the one the workspace installs for its typings
charts=$(sed -n 's/^version=\([0-9.]*\).*/\1/p' "$demoDir/highcharts.sh")
grep -qF "highcharts@$charts " "$repo/web/opc/scripts/jde-opc.sh" || die "highcharts.sh lays Highcharts '$charts', and web/opc/scripts/jde-opc.sh installs another:  set its version and integrity to that one's"
#smoke.py's modules:  the gateway socket's two proto files and what they import, Opc.Common.proto, Common.proto and
#protobuf's own timestamp and duration, whose modules the runtime has.  A file's dots are its module's packages:  proto/Opc/.
protos=( "$repo/apps/OpcGateway/src/types/proto/Opc.FromClient.proto" "$repo/apps/OpcGateway/src/types/proto/Opc.FromServer.proto"
	"$repo/libs/opc/shared/src/proto/Opc.Common.proto" "$repo/libs/app/shared/proto/Common.proto" )
includes=( -I "$protoInclude" ); for f in "${protos[@]}"; do includes+=( -I "${f%/*}" ); done
install -d "$optDir/proto"
"$protoc" "${includes[@]}" --python_out="$optDir/proto" "${protos[@]##*/}" \
	|| die "$protoc made no Python modules of ${protos[*]##*/}"
find "$optDir/proto" -type f -exec chmod 644 {} +
install -m 644 "$demoDir/demo-lib.sh" "$optDir/demo-lib.sh" #install-demo.sh's and reset.sh's site() and hubUp()
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
install -m 644 -t "$stage/etc/systemd/system" "$demoDir/jde-demo-retention.service" "$demoDir/jde-demo-retention.timer"
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
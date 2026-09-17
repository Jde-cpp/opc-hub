#!/usr/bin/env bash
# Jde OpcHub - Linux package.  Builds jde-opchub_<version>_<arch>.deb and jde-opchub-<version>-linux-<arch>.tar.gz from the
# release build tree - the counterpart of ../build-setup.ps1; README.md beside this file has the installed layout, what
# the package does on install/remove, and the per-user tarball install.
#
# Stages the tree (opt/jde-cpp, etc/jde-cpp, var/lib/Jde-Cpp, the systemd units), bundles the .so's the exes load from
# outside the system - the $REPO_DIR deps (fmt, boost, jsonnet) and LLVM's libc++/libc++abi, which the target distro
# ships an older major of - computes Depends: from what is left on the system, and runs dpkg-deb.  Every staged exe and
# .so gets RUNPATH=$ORIGIN (patchelf), so one dir per product resolves by itself:  our own are linked that way already
# (build/functions.cmake) but keep the build-tree entries behind it, and the third-party ones have no RUNPATH at all -
# libboost_json would not find libboost_container beside it, nor libjsonnet++ libjsonnet.
set -euo pipefail

usage(){ cat <<'USAGE'
usage: build-deb.sh [options]
  --build-dir <dir>    the release build tree            default: $JDE_BUILD_DIR/$JDE_COMPILER/<repo dir>/release
  --web-dist <dir>     ng build output (index.html)      default: <repo>/web/opc/my-workspace/dist/my-workspace/browser
  --skip-web           omit the Web UI (opt/jde-cpp/web and the nginx site file)
  --ua-nodesets <dir>  OPCFoundation/UA-Nodeset clone    default: $UA_NODE_SETS, else $REPO_DIR/UA-Nodeset
  --version <v>        default: CMakePresets.common.json's JDE_VERSION (2026.09.01); the release workflow passes the tag, which should equal it
  --out-dir <dir>      default: <build dir>/setup
  --maintainer <s>     control's Maintainer field        default: git config user.name <user.email>
  --no-strip           keep the debug sections (default --strip-debug: symbols stay for the stack traces, dwarf goes)
  --patchelf <path>    default: the patchelf on PATH (apt install patchelf, or the PyPI wheel: pip install patchelf)
  --no-tar             the .deb only
  -h, --help
USAGE
}
die(){ echo "build-deb.sh: $*" >&2; exit 1; }
warn(){ echo "build-deb.sh: warning: $*" >&2; }

buildDir=; webDist=; skipWeb=0; uaNodeSets=${UA_NODE_SETS:-}; version=; outDir=; maintainer=; strip=1; tar=1; patchelf=patchelf
while [ $# -gt 0 ]; do
	case "$1" in
		--build-dir) buildDir=${2:?}; shift 2;;   --build-dir=*) buildDir=${1#*=}; shift;;
		--web-dist) webDist=${2:?}; shift 2;;     --web-dist=*) webDist=${1#*=}; shift;;
		--skip-web) skipWeb=1; shift;;
		--ua-nodesets) uaNodeSets=${2:?}; shift 2;; --ua-nodesets=*) uaNodeSets=${1#*=}; shift;;
		--version) version=${2:?}; shift 2;;      --version=*) version=${1#*=}; shift;;
		--out-dir) outDir=${2:?}; shift 2;;       --out-dir=*) outDir=${1#*=}; shift;;
		--maintainer) maintainer=${2:?}; shift 2;; --maintainer=*) maintainer=${1#*=}; shift;;
		--no-strip) strip=0; shift;;
		--no-tar) tar=0; shift;;
		--patchelf) patchelf=${2:?}; shift 2;;    --patchelf=*) patchelf=${1#*=}; shift;;
		-h|--help) usage; exit 0;;
		*) usage >&2; die "unknown option '$1'";;
	esac
done

setupDir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$setupDir/../../../.." && pwd) #apps/OpcHub/setup/linux -> the repo root
[ -n "$buildDir" ] || buildDir="${JDE_BUILD_DIR:-/mnt/ram/linux}/${JDE_COMPILER:-clang++}/$(basename "$repo")/release"
[ -n "$webDist" ] || webDist="$repo/web/opc/my-workspace/dist/my-workspace/browser"
[ -n "$uaNodeSets" ] || uaNodeSets="${REPO_DIR:-$HOME/code/libs}/UA-Nodeset"
[ -n "$outDir" ] || outDir="$buildDir/setup"

#--- inputs ------------------------------------------------------------------------------------------------------------
hubExe=$buildDir/apps/OpcHub/exe/Jde.Opc.Hub
serverExe=$buildDir/apps/OpcServer/exe/Jde.Opc.Server
jdeLib=$buildDir/libs/fwk/lib/libJde.so
dbLib=$buildDir/libs/db/lib/libJde.DB.so
sqliteLib=$buildDir/libs/db/drivers/sqlite/lib/libJde.DB.Sqlite.so
appServerMod=$buildDir/apps/AppServer/config/sql/sqlite/libJde.DB.Sqlite.AppServer.so
gatewayMod=$buildDir/apps/OpcGateway/config/sql/sqlite/libJde.DB.Sqlite.OpcGateway.so
for f in "$hubExe" "$serverExe" "$jdeLib" "$dbLib" "$sqliteLib" "$appServerMod" "$gatewayMod"; do
	[ -f "$f" ] || die "missing $f - build Jde.Opc.Hub, Jde.Opc.Server, Jde.DB.Sqlite, Jde.DB.Sqlite.AppServer and Jde.DB.Sqlite.OpcGateway in the release tree first, or pass --build-dir"
done
[ $skipWeb = 1 ] || [ -f "$webDist/index.html" ] || die "no index.html under $webDist - run web/opc/scripts/setup.sh (ng build), pass --web-dist, or --skip-web"
for f in DI/Opc.Ua.Di.NodeSet2.xml IA/Opc.Ua.IA.NodeSet2.xml IA/Opc.Ua.IA.NodeSet2.examples.xml; do
	[ -f "$uaNodeSets/$f" ] || die "missing $f under $uaNodeSets - clone https://github.com/OPCFoundation/UA-Nodeset or pass --ua-nodesets"
done
for t in dpkg-deb dpkg ldd objdump; do command -v $t >/dev/null || die "$t not found (dpkg, binutils)"; done
[ $strip = 0 ] || command -v strip >/dev/null || die "strip not found (binutils) - or --no-strip"
command -v "$patchelf" >/dev/null || die "patchelf not found - apt install patchelf, or pip install patchelf and --patchelf <path>"

#--- version -----------------------------------------------------------------------------------------------------------
#The product version is CMakePresets.common.json's JDE_VERSION - 2026.09.01, the date, zeros and all: the string the C++ targets
#are built with and the Web UI's about page displays, so the package agrees with them.  --version names it outright (the release
#workflow passes the tag) and is expected to be the same string; anything else is warned about, not refused.
jdeVersion=$(sed -n 's/.*"JDE_VERSION": *"\([^"]*\)".*/\1/p' "$repo/CMakePresets.common.json" | head -1)
[ -n "$jdeVersion" ] || die "JDE_VERSION not found in $repo/CMakePresets.common.json"
[ -n "$version" ] || version=$jdeVersion
[ "$version" = "$jdeVersion" ] || warn "--version $version is not CMakePresets.common.json's JDE_VERSION $jdeVersion - the package's version and the product's will disagree"
#A deb version is [0-9][A-Za-z0-9.+~]*:  a `yyyy.MM.dd` tag is one as it is; `yyyy.MM.dd-N-gsha` (N commits past the tag)
#becomes yyyy.MM.dd+N.gsha - `+` sorts after the tag, which it is newer than; anything else (a bare sha) becomes 0+…
if [[ $version =~ ^([0-9]{4}\.[0-9]{2}\.[0-9]{2})-([0-9]+)-g([0-9a-f]+)$ ]]; then
	debVersion="${BASH_REMATCH[1]}+${BASH_REMATCH[2]}.g${BASH_REMATCH[3]}"
elif [[ $version =~ ^[0-9]{4}\.[0-9]{2}\.[0-9]{2}$ ]]; then
	debVersion=$version
else
	debVersion="0+$(printf '%s' "$version" | tr -c 'A-Za-z0-9.+~' '.')"
fi
if [ -z "$maintainer" ]; then
	name=$(git -C "$repo" config user.name || true); email=$(git -C "$repo" config user.email || true)
	if [ -n "$name" ] && [ -n "$email" ]; then maintainer="$name <$email>"; else maintainer="Jde-Cpp <noreply@jde-cpp.invalid>"; fi
fi
arch=$(dpkg --print-architecture)

#--- stage -------------------------------------------------------------------------------------------------------------
mkdir -p "$outDir"
stage=$(mktemp -d "$outDir/stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT
optDir=$stage/opt/jde-cpp; etcDir=$stage/etc/jde-cpp; dataDir=$stage/var/lib/Jde-Cpp
unitDir=$stage/usr/lib/systemd/system; docDir=$stage/usr/share/doc/jde-opchub

#binaries - one dir per product carries everything it loads; the sqlite driver and the proc MODULEs beside the exe are
#where args/install's $(ExeDir) looks
install -d "$optDir/opchub" "$optDir/opcserver"
install -m 755 "$hubExe" "$optDir/opchub/"
install -m 644 "$jdeLib" "$dbLib" "$sqliteLib" "$appServerMod" "$gatewayMod" "$optDir/opchub/"
install -m 755 "$serverExe" "$optDir/opcserver/"
install -m 644 "$jdeLib" "$dbLib" "$sqliteLib" "$optDir/opcserver/"

#Every .so an exe or module resolves outside the system dirs comes along (the deps tree: fmt, boost, jsonnet), and of
#the system ones LLVM's libc++/libc++abi (and libunwind, should libc++abi ever link it).  What to copy comes from ldd on
#the *originals* - their build RUNPATHs still reach the deps tree, and ldd lists the whole closure as the loader would
#resolve it for that exe, a bundled lib's own deps included.  Then every staged file gets RUNPATH=$ORIGIN, and ldd on
#the staged copies must resolve everything beside the exe or on the system - the target's view.
bundle(){ #dir original...
	local dir=$1 name path; shift
	for f in "$@"; do
		if ldd "$f" | grep -q 'not found'; then die "$f: $(ldd "$f" | grep 'not found' | tr -s ' \t' ' ' | tr '\n' ';') - the build tree does not resolve its own deps"; fi
	done
	while read -r name path; do
		[ -n "$path" ] && [ ! -e "$dir/$name" ] || continue
		case "$path" in
			/lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*)
				case "$name" in libc++.so.*|libc++abi.so.*|libunwind.so.*) ;; *) continue;; esac;;
		esac
		install -m 644 "$(realpath "$path")" "$dir/$name"
	done < <(for f in "$@"; do ldd "$f" | awk '/ => \//{print $1, $3}'; done | sort -u)
	for f in "$dir"/*; do "$patchelf" --set-rpath '$ORIGIN' "$f"; done
	for f in "$dir"/*; do
		if ldd "$f" | grep -q 'not found'; then die "$f: $(ldd "$f" | grep 'not found' | tr -s ' \t' ' ' | tr '\n' ';')"; fi
	done
}
bundle "$optDir/opchub" "$hubExe" "$jdeLib" "$dbLib" "$sqliteLib" "$appServerMod" "$gatewayMod"
bundle "$optDir/opcserver" "$serverExe" "$jdeLib" "$dbLib" "$sqliteLib"
binaries=( "$optDir"/opchub/* "$optDir"/opcserver/* ) #every ELF in the package - not opt/jde-cpp/web
if [ $strip = 1 ]; then
	for f in "${binaries[@]}"; do strip --strip-debug "$f"; done
fi

#the settings mirror - the hub config imports the AppServer's and the gateway's by repo-relative path and the gateway's
#introspection files by Settings::Directory()-relative path, so the repo layout is kept (dpkg conffiles: edits survive an upgrade)
install -D -m 644 -t "$etcDir/apps/OpcHub/config" "$repo/apps/OpcHub/config/Opc.Hub.jsonnet"
install -D -m 644 -t "$etcDir/apps/OpcHub/config/args/install" "$repo/apps/OpcHub/config/args/install/args.libsonnet"
install -D -m 644 -t "$etcDir/apps/AppServer/config" "$repo/apps/AppServer/config/App.Server.jsonnet"
install -D -m 644 -t "$etcDir/apps/OpcGateway/config" "$repo/apps/OpcGateway/config/Opc.Gateway.jsonnet"
install -D -m 644 -t "$etcDir/apps/OpcGateway/config/introspection" "$repo"/apps/OpcGateway/config/introspection/*.jsonnet
install -D -m 644 -t "$etcDir/libs/db/config" "$repo/libs/db/config/paths-common.libsonnet"
install -D -m 644 -t "$etcDir/apps/OpcServer/config" "$repo/apps/OpcServer/config/Opc.Server.jsonnet" "$repo/apps/OpcServer/config/Opc.Server.Install.jsonnet"
install -D -m 644 -t "$etcDir/apps/OpcServer/config/args/install" "$repo/apps/OpcServer/config/args/install/args.libsonnet"
install -D -m 644 -t "$etcDir/apps/OpcServer/config/pubsub" "$repo/apps/OpcServer/config/pubsub/pumps.libsonnet"
install -m 640 "$setupDir/env" "$etcDir/env"
[ $skipWeb = 1 ] || install -m 644 "$setupDir/nginx-opchub.conf" "$etcDir/nginx-opchub.conf"

#the products' data dirs - meta/sql flat, where args/install points (common-meta from libs/db: the copies beside the app
#metas in the repo are configure-time links).  sql/ and nodesets/ are package-owned: replaced on an upgrade, removed with it.
install -D -m 644 -t "$dataDir/OpcHub" "$repo/libs/access/config/access-meta.jsonnet" "$repo/libs/access/config/access-ql.jsonnet" \
	"$repo/apps/AppServer/config/app-meta.jsonnet" "$repo/apps/OpcGateway/config/opcGateway-meta.jsonnet" "$repo/libs/db/config/common-meta.libsonnet"
install -D -m 644 "$repo/libs/access/config/release.mutation" "$dataDir/OpcHub/sql/access.mutation" #<schema>*.mutation - the release seed, not the dev one
install -D -m 644 "$repo/libs/access/config/release.roles" "$dataDir/OpcHub/sql/access.roles" #<schema>*.roles - the roles (Viewer … Owner), applied after the access server is configured (appStartup.cpp), as the nsi's SEC_HUB does
#the OPC UA server's seeds (reviews/install-issues.md #1): the Web UI's Google provider, the server as the hub's default connection
#and its provider row.  The package ships the server, so always; the tarball's install.sh drops them without --opcserver.  The
#underscore names sort after access.mutation, whose provider type 7 (OpcServer) they reference.
install -m 644 "$repo/libs/access/config/release-google.mutation" "$dataDir/OpcHub/sql/access_google.mutation"
install -m 644 "$repo/libs/access/config/release-opcServer.mutation" "$dataDir/OpcHub/sql/access_opcServer.mutation"
install -m 644 "$repo/apps/OpcGateway/config/release-opcServer.mutation" "$dataDir/OpcHub/sql/gateway_opcServer.mutation"
install -m 644 "$repo/libs/access/config/release-opcServer.roles" "$dataDir/OpcHub/sql/access_opcServer.roles" #reviews/install-issues.md #25: the OPC Server Instance role - Administer on opc.install nodeIds - so the machine grant is one tick.  A *.roles, applied after access server config.
install -m 644 -t "$dataDir/OpcHub/sql" "$repo/apps/AppServer/config/app.mutation" \
	"$repo"/libs/access/config/sql/sqlite/*.sql "$repo"/apps/AppServer/config/sql/sqlite/*.sql "$repo"/apps/OpcGateway/config/sql/sqlite/*.sql #the sqlite views; the procs are compiled into the MODULEs
install -D -m 644 -t "$dataDir/OpcServer" "$repo/libs/access/config/access-meta.jsonnet" "$repo/libs/access/config/access-ql.jsonnet" \
	"$repo/libs/db/config/common-meta.libsonnet" "$repo/apps/OpcServer/config/opcServer-meta.jsonnet"
install -D -m 644 -t "$dataDir/OpcServer/nodesets" "$uaNodeSets/DI/Opc.Ua.Di.NodeSet2.xml" "$uaNodeSets/IA/Opc.Ua.IA.NodeSet2.xml" \
	"$uaNodeSets/IA/Opc.Ua.IA.NodeSet2.examples.xml" "$repo/apps/OpcServer/config/nodesets/pumps.NodeSet2.xml"

if [ $skipWeb = 0 ]; then
	install -d "$optDir/web"
	cp -rL "$webDist/." "$optDir/web/"
	find "$optDir/web" -type f -name '*.map' -delete #not the source maps - ~7 MB a browser never asks for unless devtools are open (as the Windows installer)
	find "$optDir/web" -type d -exec chmod 755 {} +
	find "$optDir/web" -type f -exec chmod 644 {} +
fi
install -D -m 644 -t "$unitDir" "$setupDir/jde-opchub.service" "$setupDir/jde-opcserver.service"
install -D -m 644 "$setupDir/README.md" "$docDir/README.md"
install -m 644 "$repo/LICENSE" "$docDir/copyright"

#--- DEBIAN ------------------------------------------------------------------------------------------------------------
#Depends: the package owning each system .so the staged binaries still resolve to (the build machine's names - libssl3t64
#on noble), libc6 at the highest GLIBC_x.y symbol version any of them imports, adduser for the postinst, and tzdata -
#libc++'s chrono reads /usr/share/zoneinfo (the proto logger dies without it: "corrupt tzdb", seen on a minimal image).
depends(){
	local name path pkg
	while read -r name path; do
		case "$path" in "$optDir"/*) continue;; esac
		pkg=$(dpkg -S "$path" 2>/dev/null | head -1 | cut -d: -f1) || true
		[ -n "$pkg" ] || pkg=$(dpkg -S "$(realpath "$path")" 2>/dev/null | head -1 | cut -d: -f1) || true
		[ -n "$pkg" ] || die "no package owns $path (needed by the staged binaries) - install it from a package, or bundle it"
		echo "$pkg"
	done < <(for f in "${binaries[@]}"; do ldd "$f" | awk '/ => \//{print $1, $3}'; done | sort -u) | sort -u
}
glibc=$(for f in "${binaries[@]}"; do objdump -T "$f" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' || true; done | sed 's/GLIBC_//' | sort -Vu | tail -1)
[ -n "$glibc" ] || die "no GLIBC_x.y symbol versions found in the staged binaries"
dependsList=$( { depends | sed "s/^libc6\$/libc6 (>= $glibc)/"; echo adduser; echo tzdata; } | sort -u | paste -sd, | sed 's/,/, /g' )

deb=$stage/DEBIAN
install -d -m 755 "$deb"
install -m 755 -t "$deb" "$setupDir/debian/postinst" "$setupDir/debian/prerm" "$setupDir/debian/postrm"
find "$etcDir" -type f | sed "s|^$stage||" | sort > "$deb/conffiles"
size=$(du -sk --exclude=DEBIAN "$stage" | cut -f1)
sed -e "s|@VERSION@|$debVersion|" -e "s|@ARCH@|$arch|" -e "s|@MAINTAINER@|$maintainer|" -e "s|@SIZE@|$size|" -e "s|@DEPENDS@|$dependsList|" \
	"$setupDir/debian/control.in" > "$deb/control"
find "$stage" -type f -not -path "$stage/DEBIAN/*" -printf '%P\n' | sort | while read -r f; do
	echo "$(md5sum < "$stage/$f" | cut -d' ' -f1)  $f"
done > "$deb/md5sums"
find "$stage" -type d -exec chmod 755 {} +

debFile=$outDir/jde-opchub_${debVersion}_${arch}.deb
dpkg-deb --build --root-owner-group "$stage" "$debFile"
echo "built $debFile ($(du -h "$debFile" | cut -f1)) - Depends: $dependsList"

#the per-user install:  the same trees plus install.sh (README.md) - no DEBIAN, no /usr/share/doc
if [ $tar = 1 ]; then
	tarFile=$outDir/jde-opchub-${debVersion}-linux-${arch}.tar.gz
	install -m 755 "$setupDir/install.sh" "$stage/install.sh"
	install -m 644 "$setupDir/README.md" "$stage/README.md"
	tar -czf "$tarFile" --owner=0 --group=0 --numeric-owner -C "$stage" --exclude=./usr/share ./opt ./etc ./var ./usr ./install.sh ./README.md
	echo "built $tarFile ($(du -h "$tarFile" | cut -f1))"
fi

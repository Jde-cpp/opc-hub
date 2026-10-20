#!/usr/bin/env bash
# Jde OpcHub - Linux package.  Builds jde-opchub_<version>_<arch>.deb and jde-opchub-<version>-linux-<arch>.tar.gz from the
# release build tree - the counterpart of ../build-setup.ps1; README.md beside this file has the installed layout, what
# the package does on install/remove, and the per-user tarball install.
#
# Stages the tree (opt/jde-cpp, etc/jde-cpp, var/lib/Jde-Cpp, the systemd units), bundles the .so's the exes load from
# outside the system - the $REPO_DIR deps (fmt, abseil, boost, jsonnet) and LLVM's libc++/libc++abi, which the target distro
# ships an older major of - computes Depends: from what is left on the system, and runs dpkg-deb; the tarball then also
# carries the Depends: a system may lack (tarballPkgs), which nothing installs for it.  Every staged exe and
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
  --version <v>        default: `git describe --tags` - the tag itself on a tag, <tag>-N-gsha past one, and
                       CMakePresets.common.json's JDE_VERSION where no tag is reachable; the release
                       workflow passes the tag, and an empty string on a run that has none - so `--version ""` is the
                       default, not an error
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
		--version) version=${2?}; shift 2;;       --version=*) version=${1#*=}; shift;;
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
#../files.txt, the list the Windows installer reads too:  `section ships to from [as]` a line.  Kept here are this platform's
#lines - all, linux, and user, the current-user overlays, which go into the tarball alone.
manifest=$(dirname "$setupDir")/files.txt
mSection=(); mShips=(); mTo=(); mFrom=(); mAs=()
while IFS= read -r line || [ -n "$line" ]; do #the last line has no newline
	read -r section ships to from as rest <<< "${line%%#*}"
	[ -n "$section" ] || continue
	[ -n "$from" ] && [ -z "$rest" ] || die "$manifest: '$line' - want section ships to from [as]"
	case "$section" in hub|opcserver) ;; *) die "$manifest: section '$section' is neither hub nor opcserver";; esac
	case "$ships" in all|linux|user) ;; windows) continue;; *) die "$manifest: ships '$ships' is none of all, windows, linux, user";; esac
	mSection+=( "$section" ); mShips+=( "$ships" ); mTo+=( "${to%!}" ); mFrom+=( "$from" ); mAs+=( "$as" )
done < "$manifest"
sources(){ #i - entry i's files:  a build: or nodesets: path under the build tree or the UA-Nodeset clone, else the repo's;  a * expands
	local root=$repo from=${mFrom[$1]}
	case "$from" in build:*) root=$buildDir; from=${from#build:};; nodesets:*) root=$uaNodeSets; from=${from#nodesets:};; esac
	printf '%s\n' "$root"/$from
}
for i in "${!mFrom[@]}"; do
	while read -r f; do
		[ -f "$f" ] && continue
		case "${mFrom[$i]}" in
			build:*) die "missing $f - build Jde.Opc.Hub, Jde.Opc.Server, Jde.DB.Sqlite, Jde.DB.Sqlite.AppServer, Jde.DB.Sqlite.OpcGateway and Jde.DB.MySql in the release tree first, or pass --build-dir";;
			nodesets:*) die "missing $f - clone https://github.com/OPCFoundation/UA-Nodeset or pass --ua-nodesets";;
			*) die "missing $f, which $manifest names";;
		esac
	done < <(sources "$i")
done
[ $skipWeb = 1 ] || [ -f "$webDist/index.html" ] || die "no index.html under $webDist - run web/opc/scripts/setup.sh (ng build), pass --web-dist, or --skip-web"
for t in dpkg-deb dpkg ldd objdump; do command -v $t >/dev/null || die "$t not found (dpkg, binutils)"; done
[ $strip = 0 ] || command -v strip >/dev/null || die "strip not found (binutils) - or --no-strip"
command -v "$patchelf" >/dev/null || die "patchelf not found - apt install patchelf, or pip install patchelf and --patchelf <path>"

#--- version -----------------------------------------------------------------------------------------------------------
#The product version is CMakePresets.common.json's JDE_VERSION - a yyyy.MM.dd date, zeros and all: the string the C++ targets
#are built with and the Web UI's about page displays, so the package agrees with them.  --version names it outright (the release
#workflow passes the tag) and is expected to carry that same string; anything else is warned about, not refused.
jdeVersion=$(sed -n 's/.*"JDE_VERSION": *"\([^"]*\)".*/\1/p' "$repo/CMakePresets.common.json" | head -1)
[ -n "$jdeVersion" ] || die "JDE_VERSION not found in $repo/CMakePresets.common.json"
#Without one it is `git describe`, not JDE_VERSION alone:  dpkg compares versions and not contents, so a build past the tag
#calling itself the tag installs over that release as a no-op, and `dpkg -l` cannot tell the two apart (reviews/install-issues.md
##27).  Describe answers the tag itself on a tag and <tag>-N-gsha past one, which becomes <tag>+N.gsha below; `20*` is the
#release workflow's own tag filter.  Not --dirty:  that suffix is not a deb version and would land in the 0+ bucket, which
#sorts before every release.  No tag reachable - a shallow clone, an export, no git - leaves JDE_VERSION, as before.
if [ -z "$version" ]; then
	version=$(git -C "$repo" describe --tags --match '20*' 2>/dev/null || true)
	[ -n "$version" ] || version=$jdeVersion
fi
#the release the package claims, so the tag part is what is compared:  <tag>-N-gsha carries JDE_VERSION and is no disagreement
[ "${version%%-*}" = "$jdeVersion" ] || warn "version $version does not carry CMakePresets.common.json's JDE_VERSION $jdeVersion - the package's version and the product's will disagree"
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

#files.txt's files:  the binaries into each product's dir, the exe 755, and the settings mirror (dpkg conffiles, so edits
#survive an upgrade) and the data dirs (package-owned:  replaced on an upgrade, removed with it) under etc and var/lib.  Each
#binary's original goes on its product's list, for bundle.
declare -A programDir=( [hub]=opchub [opcserver]=opcserver )
hubBins=(); serverBins=()
place(){ #ships... - stages the entries shipped as one of these
	local i f dir mode
	for i in "${!mFrom[@]}"; do
		case " $* " in *" ${mShips[$i]} "*) ;; *) continue;; esac
		case "${mTo[$i]}" in
			program) dir=$optDir/${programDir[${mSection[$i]}]};;
			config/*) dir=$etcDir/${mTo[$i]#config/};;
			data/*) dir=$dataDir/${mTo[$i]#data/};;
			*) die "$manifest: to '${mTo[$i]}' is none of program, config/<dir>, data/<dir>";;
		esac
		while read -r f; do
			mode=644
			if [ "${mTo[$i]}" = program ]; then
				case "$f" in *.so|*.so.*) ;; *) mode=755;; esac
				if [ "${mSection[$i]}" = hub ]; then hubBins+=( "$f" ); else serverBins+=( "$f" ); fi
			fi
			install -D -m $mode "$f" "$dir/${mAs[$i]:-$(basename "$f")}"
		done < <(sources "$i")
	done
}
place all linux

#Every .so an exe or module resolves outside the system dirs comes along (the deps tree: fmt, abseil, boost, jsonnet), and of
#the system ones LLVM's libc++/libc++abi (and libunwind, should libc++abi ever link it) plus libxml2 and the ICU pair it
#links.  libxml2 is the one system library whose soname moves between the releases we claim to support - Ubuntu 24.04
#builds against libxml2.so.2, 26.04 ships only libxml2.so.16 (package `libxml2-16`, and `libicu74` -> `libicu78`) - so a
#package built on the older release could not install on the newer one, and forcing it past apt left Jde.Opc.Server
#unable to load at all (reviews/install-issues.md #28).  Carrying it costs ~8.6 MB compressed, almost all of it
#libicudata, and is what makes "24.04 or later" true.  What to copy comes from ldd on
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
				case "$name" in libc++.so.*|libc++abi.so.*|libunwind.so.*|libxml2.so.*|libicuuc.so.*|libicudata.so.*) ;; *) continue;; esac;;
		esac
		install -m 644 "$(realpath "$path")" "$dir/$name"
	done < <(for f in "$@"; do ldd "$f" | awk '/ => \//{print $1, $3}'; done | sort -u)
	for f in "$dir"/*; do "$patchelf" --set-rpath '$ORIGIN' "$f"; done
	for f in "$dir"/*; do
		if ldd "$f" | grep -q 'not found'; then die "$f: $(ldd "$f" | grep 'not found' | tr -s ' \t' ' ' | tr '\n' ';')"; fi
	done
}
bundle "$optDir/opchub" "${hubBins[@]}"
bundle "$optDir/opcserver" "${serverBins[@]}"
binaries=( "$optDir"/opchub/* "$optDir"/opcserver/* ) #every ELF in the package - not opt/jde-cpp/web
if [ $strip = 1 ]; then
	for f in "${binaries[@]}"; do strip --strip-debug "$f"; done
fi

install -D -m 644 "$setupDir/env" "$stage/usr/share/jde-opchub/env" #the template postinst lays at /etc/jde-cpp/env when absent - not a conffile, which a purge deletes with the kept keys' passcode in it (reviews/m4-closing.md #6)
[ $skipWeb = 1 ] || install -m 644 "$setupDir/nginx-opchub.conf" "$etcDir/nginx-opchub.conf"

if [ $skipWeb = 0 ]; then
	install -d "$optDir/web"
	cp -rL "$webDist/." "$optDir/web/"
	find "$optDir/web" -type f -name '*.map' -delete #not the source maps - ~7 MB a browser never asks for unless devtools are open (as the Windows installer)
	rm -rf "$optDir/web/addons" #nor a site's own add-ons - the trend's Highcharts, which the install never carries (#302; the help's "Trend chart")
	find "$optDir/web" -type d -exec chmod 755 {} +
	find "$optDir/web" -type f -exec chmod 644 {} +
fi
install -D -m 644 -t "$unitDir" "$setupDir/jde-opchub.service" "$setupDir/jde-opcserver.service"
install -D -m 644 "$setupDir/README.md" "$docDir/README.md"
install -m 644 "$repo/LICENSE" "$docDir/copyright"
install -m 644 "$repo/THIRD-PARTY-NOTICES.txt" "$docDir/THIRD-PARTY-NOTICES.txt" #the bundled .so's and what is linked into ours (build/third-party-notices.sh)

#--- DEBIAN ------------------------------------------------------------------------------------------------------------
#Depends: the package owning each system .so the staged binaries still resolve to (the build machine's names - libssl3t64
#on noble), libc6 at the highest GLIBC_x.y symbol version any of them imports, adduser for the postinst, and tzdata -
#libc++'s chrono reads /usr/share/zoneinfo (the proto logger dies without it: "corrupt tzdb", seen on a minimal image).
owner(){ #path - the package that owns it, or nothing
	local pkg
	pkg=$(dpkg -S "$1" 2>/dev/null | head -1 | cut -d: -f1) || true
	[ -n "$pkg" ] || pkg=$(dpkg -S "$(realpath "$1")" 2>/dev/null | head -1 | cut -d: -f1) || true
	echo "$pkg"
}
depends(){
	local name path pkg
	while read -r name path; do
		case "$path" in "$optDir"/*) continue;; esac
		pkg=$(owner "$path")
		[ -n "$pkg" ] || die "no package owns $path (needed by the staged binaries) - install it from a package, or bundle it"
		echo "$pkg"
	done < <(for f in "${binaries[@]}"; do ldd "$f" | awk '/ => \//{print $1, $3}'; done | sort -u) | sort -u
}
glibc=$(for f in "${binaries[@]}"; do objdump -T "$f" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' || true; done | sed 's/GLIBC_//' | sort -Vu | tail -1)
[ -n "$glibc" ] || die "no GLIBC_x.y symbol versions found in the staged binaries"
dependsList=$( { depends | sed "s/^libc6\$/libc6 (>= $glibc)/"; echo adduser; echo tzdata; } | sort -u | paste -sd, | sed 's/,/, /g' )

deb=$stage/DEBIAN
install -d -m 755 "$deb"
install -m 755 -t "$deb" "$setupDir/debian/preinst" "$setupDir/debian/postinst" "$setupDir/debian/prerm" "$setupDir/debian/postrm"
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
#Under one top-level directory, the archive's own name:  the stage's children packed bare scattered six entries over
#whatever the extract directory already held, and the README's `cd` into a directory nothing created could not work
#(reviews/install-issues.md #29).  --transform renames the members on the way in - the stage stays as dpkg-deb needs it -
#and the `s` scope is turned off so a symlink target is never rewritten with it.
if [ $tar = 1 ]; then
	tarRoot=jde-opchub-${debVersion}-linux-${arch}
	tarFile=$outDir/$tarRoot.tar.gz
	install -m 755 "$setupDir/install.sh" "$stage/install.sh"
	install -m 644 "$setupDir/README.md" "$stage/README.md"
	install -m 644 "$repo/LICENSE" "$repo/THIRD-PARTY-NOTICES.txt" "$stage/" #usr/share is excluded below, so the licenses ride at the top
	place user #files.txt's current-user overlays, args/install-user:  added here and not above, since the .deb's units run args/install
	install -m 640 "$setupDir/env" "$etcDir/env" #install.sh's template for $dataRoot/env; the package's is under usr/share, which this archive leaves out
	#The Depends: a system may lack ride along:  apt installs the .deb's, but nothing installs the tarball's - its install needs
	#no root, and getting a package does.  liburing2 is optional, absent from a 24.04 that never had the .deb, and without it
	#neither product loads (reviews/install-issues.md #59).  They go beside the exes that load them, where RUNPATH=$ORIGIN finds
	#them first; the toolchain floor and openssl stay the system's - its glibc is paired with its own loader, and apt updates its
	#openssl.  Both lists are named, not read from dpkg's Priority:  that is the build machine's status file, and the release
	#runner's records libc6 and the rest as optional, which bundled all of them (#64).  A package in neither list fails the
	#build until it is placed.  Added after dpkg-deb, as the overlays above, so the .deb keeps its Depends: instead.
	tarballPkgs=' liburing2 '
	systemPkgs=' libc6 libgcc-s1 libstdc++6 libssl3t64 libzstd1 liblzma5 zlib1g '
	bundled=' '
	for dir in "$optDir/opchub" "$optDir/opcserver"; do
		while read -r name path; do
			case "$path" in "$optDir"/*) continue;; esac
			pkg=$(owner "$path")
			case "$systemPkgs" in *" $pkg "*) continue;; esac
			case "$tarballPkgs" in *" $pkg "*) ;; *) die "$path (${pkg:-no package}) is in neither build-deb.sh's tarballPkgs nor its systemPkgs - name it in one: the tarball carries the first, a system must have the second";; esac
			install -m 644 "$(realpath "$path")" "$dir/$name"
			"$patchelf" --set-rpath '$ORIGIN' "$dir/$name" #a bundled one's own deps resolve beside it too - RUNPATH is not inherited
			echo "tarball: ${dir#"$stage"/}/$name from $pkg, which a system may lack"
			bundled="$bundled$pkg "
		done < <(for f in "$dir"/*; do ldd "$f" | awk '/ => \//{print $1, $3}'; done | sort -u)
	done
	for pkg in $tarballPkgs; do
		case "$bundled" in *" $pkg "*) ;; *) die "tarballPkgs names $pkg, which nothing staged loads - drop it";; esac
	done
	tar -czf "$tarFile" --owner=0 --group=0 --numeric-owner -C "$stage" --exclude=./usr/share \
		--transform "s,^\./,$tarRoot/,S" ./opt ./etc ./var ./usr ./install.sh ./README.md ./LICENSE ./THIRD-PARTY-NOTICES.txt
	echo "built $tarFile ($(du -h "$tarFile" | cut -f1)) - unpacks into $tarRoot/"
fi
#!/usr/bin/env bash
# The public demo's charts (docs/demo/plan.md Phase 10;  README.md beside this file is the runbook):  Highcharts Stock, the add-on
# the OEE pages' charts and the History tab's trend draw with, laid under the site as the help's "Trend chart" has it.  Neither
# the jde-opchub package nor the demo's tarball carries it:  its licence is a commercial one, and the site's owner holds the one a
# public site needs.  Run it once that licence is held;  without the add-on each of those pages says so and shows its tables.
#   sudo /opt/jde-cpp/demo/highcharts.sh [--from <file>] [--version <v> --integrity <sha512-...>]
#   sudo /opt/jde-cpp/demo/highcharts.sh --remove
#   /opt/jde-cpp/demo/highcharts.sh --check
#     --from        the npm package's tarball, highcharts-<v>.tgz, already on the box      default: fetched from registry.npmjs.org
#     --version     another version than the one this release's site is written against, with --integrity, the package's
#                   `dist.integrity` as `npm view highcharts@<v> dist.integrity` prints it
#     --remove      takes the add-on out
#     --check       says whether it is there and at this release's version:  the exit is 0 where it is
# It lays the package's esm folder, its minified .js files alone, at /opt/jde-cpp/web/addons/highcharts/esm, which the hub serves
# at addons/highcharts/esm/, and the version beside it.  The tarball is checked against the integrity before anything is laid,
# and the folder there is replaced whole.  A package upgrade leaves the folder, which is not the package's.
set -euo pipefail
die(){ echo "highcharts.sh: $*" >&2; exit 1; }
version=13.1.1 #as web/opc/scripts/jde-opc.sh installs it for the typings;  build-demo.sh refuses a tarball where the two differ
integrity=sha512-k/smhxCgaqHDsuy9ZCWs8FJNTPrbzc8BmMA4M/8f9JMwv9HgmNZjulyWwZ9q9ly6AzHpskL9mhlW8vgsSqnstg== #that version's, as the registry and the workspace's lock have it
site=${JDE_SITE:-/opt/jde-cpp/web} #the hub's http.site, args/install's siteDir
addon=$site/addons/highcharts

from=; asked=; given=; remove=0; check=0
while [ $# -gt 0 ]; do
	case "$1" in
		--from) from=${2:?}; shift 2;;            --from=*) from=${1#*=}; shift;;
		--version) asked=${2:?}; shift 2;;        --version=*) asked=${1#*=}; shift;;
		--integrity) given=${2:?}; shift 2;;      --integrity=*) given=${1#*=}; shift;;
		--remove) remove=1; shift;;
		--check) check=1; shift;;
		-h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		*) die "unknown option '$1'";;
	esac
done

if [ $check = 1 ]; then
	there=$(cat "$addon/VERSION" 2>/dev/null || true)
	if [ ! -f "$addon/esm/highstock.js" ]; then
		echo "Highcharts is not on the site:  the OEE pages and the trend show their tables.  Once its licence is held:  sudo $0"
		exit 1
	elif [ "$there" != "$version" ]; then
		echo "Highcharts ${there:-of no known version} is on the site, and this release's site is written against $version:  sudo $0"
		exit 1
	fi
	echo "Highcharts $version is on the site"
	exit 0
fi
[ "$(id -u)" = 0 ] || die "run it as root:  the site's folder is root's"
[ -d "$site" ] || die "no $site - the jde-opchub package comes first"
if [ $remove = 1 ]; then
	rm -rf "$addon"
	rmdir --ignore-fail-on-non-empty "$site/addons" 2>/dev/null || true
	echo "highcharts.sh: removed $addon:  the OEE pages and the trend show their tables"
	exit 0
fi
if [ -n "$asked" ] && [ "$asked" != "$version" ]; then
	[ -n "$given" ] || die "--version $asked takes --integrity, that package's dist.integrity:  only $version's is known here"
	version=$asked; integrity=$given
elif [ -n "$given" ]; then
	integrity=$given
fi
case "$integrity" in sha512-*) ;; *) die "--integrity is npm's, sha512-<base64>";; esac
for t in tar openssl; do command -v $t >/dev/null || die "$t not found"; done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
if [ -z "$from" ]; then
	command -v curl >/dev/null || die "curl not found - or pass --from <highcharts-$version.tgz>"
	from=$tmp/highcharts-$version.tgz
	curl -fsS --max-time 300 -o "$from" "https://registry.npmjs.org/highcharts/-/highcharts-$version.tgz" \
		|| die "could not fetch highcharts-$version.tgz from registry.npmjs.org - or pass --from <file>"
fi
[ -f "$from" ] || die "no $from"
[ "sha512-$(openssl dgst -sha512 -binary "$from" | openssl base64 -A)" = "$integrity" ] \
	|| die "$from is not highcharts $version:  its sha512 is not the one expected.  Nothing was laid"
tar -xzf "$from" -C "$tmp" package/esm || die "$from holds no package/esm"
find "$tmp/package/esm" -type f \( ! -name '*.js' -o -name '*.src.js' \) -delete #the minified modules alone, as a dev build's
find "$tmp/package/esm" -type d -empty -delete
for f in highstock.js highcharts.js modules/accessibility.js modules/xrange.js; do #what the pages import (highcharts-loader.ts)
	[ -f "$tmp/package/esm/$f" ] || die "highcharts $version has no esm/$f, which the site imports.  Nothing was laid"
done

rm -rf "$addon"
install -d -m 755 "$addon"
cp -r "$tmp/package/esm" "$addon/esm"
printf '%s' "$version" > "$addon/VERSION"
chown -R root:root "$site/addons"
find "$site/addons" -type d -exec chmod 755 {} +
find "$site/addons" -type f -exec chmod 644 {} +
echo "highcharts.sh: laid Highcharts $version at $addon/esm, $(find "$addon/esm" -type f | wc -l) files, $(du -sh "$addon/esm" | cut -f1):  a page loaded from now on draws its charts"
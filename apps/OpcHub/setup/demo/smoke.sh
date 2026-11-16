#!/usr/bin/env bash
# The public demo's smoke, its HTTP half (docs/demo/plan.md Phase 8;  Phase 10 adds the socket's):  what a visitor's browser
# meets at the public name, and with --hub what the owner meets at the hub itself.  Run it after every deploy.
#   smoke.sh [--url <site>] [--resolve <address>] [--insecure] [--hub <hub>]
#     --url        the site                                   default: https://opchub.trypoply.com
#     --resolve    dial the site's name at this address:  127.0.0.1 on the box, or before the DNS records exist
#     --insecure   take any certificate - a container's self-signed pair.  TLS is then not checked, and the run says so
#     --hub        the hub, not through nginx:  http://127.0.0.1:1967 on the box, or the tunnel's end.  The owner's
#                  session is $JDE_SESSION, a signed-in browser's, else as root on the box the seed's (seed.sh session)
# Each check is a line, `ok` or `FAILED`;  the exit is 0 when none failed.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
url=https://opchub.trypoply.com; resolve=; insecure=0; hub=
while [ $# -gt 0 ]; do
	case "$1" in
		--url) url=${2:?}; shift 2;;          --url=*) url=${1#*=}; shift;;
		--resolve) resolve=${2:?}; shift 2;;  --resolve=*) resolve=${1#*=}; shift;;
		--insecure) insecure=1; shift;;
		--hub) hub=${2:?}; shift 2;;          --hub=*) hub=${1#*=}; shift;;
		-h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		*) echo "smoke.sh: unknown option '$1'" >&2; exit 2;;
	esac
done
url=${url%/}; hub=${hub%/}
host=${url#*://}; host=${host%%[:/]*}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

failed=0
ok(){ echo "ok      $*"; }
fail(){ echo "FAILED  $*"; failed=1; }
check(){ #description, then a test's words
	local what=$1; shift
	if "$@"; then ok "$what"; else fail "$what"; fi
}
#get <name> <curl args...> - the status on stdout, the headers in $tmp/<name>.h, lower-cased, and the body in $tmp/<name>.b
get(){
	local name=$1 status; shift
	status=$(curl -sS --max-time 30 -o "$tmp/$name.b" -D "$tmp/$name.raw" -w '%{http_code}' "$@" 2>"$tmp/$name.err") || status=000
	tr -d '\r' < "$tmp/$name.raw" 2>/dev/null | tr 'A-Z' 'a-z' > "$tmp/$name.h"
	echo "$status"
}
site=() #the site's name at --resolve's address, on both ports
[ -z "$resolve" ] || site=( --resolve "$host:443:$resolve" --resolve "$host:80:$resolve" )
tls=(); [ $insecure = 0 ] || tls=( -k )
header(){ grep -q "^$2" "$tmp/$1.h"; } #name, a lower-case header line's start
json(){ python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if eval(sys.argv[2]) else 1)' "$tmp/$1.b" "$2" 2>/dev/null; } #name, a python test of `d`
ql(){ #name session query - a POST to the site's /graphql, or to the hub's where name starts with hub
	local name=$1 session=$2 base=$url; shift 2
	local args=( "${site[@]}" "${tls[@]}" )
	case "$name" in hub*) base=$hub; args=();; esac
	python3 -c 'import json,sys; print(json.dumps({"query": sys.argv[1]}))' "$1" > "$tmp/$name.q"
	get "$name" "${args[@]}" -X POST "$base/graphql" -H 'Content-Type: application/json' -H "Authorization: $session" --data-binary "@$tmp/$name.q"
}

#--- the front ---------------------------------------------------------------------------------------------------------
if [ $insecure = 1 ]; then
	echo "skipped the certificate's check (--insecure)"
else
	check "TLS:  $url's certificate is valid for $host" [ "$(get tls "${site[@]}" "$url/")" = 200 ]
fi
status=$(get redirect "${site[@]}" "http://$host/any?x=1")
check "port 80 redirects to https ($status)" [ "$status" = 301 ]
check "  to the same path on https://$host" header redirect "location: https://$host/any?x=1"
status=$(get page "${site[@]}" "${tls[@]}" "$url/")
check "the page answers ($status)" [ "$status" = 200 ]
check "  as html" header page 'content-type: text/html'
check "  Strict-Transport-Security" header page 'strict-transport-security: max-age='
check "  X-Frame-Options DENY" header page 'x-frame-options: deny'
check "  a Content-Security-Policy, enforced or report-only" header page 'content-security-policy'

#--- config.json -------------------------------------------------------------------------------------------------------
status=$(get config "${site[@]}" "${tls[@]}" "$url/config.json")
check "config.json answers ($status)" [ "$status" = 200 ]
check "  as json" header config 'content-type: application/json'
check "  no-cache" header config 'cache-control: no-cache'
check "  the page's own port, and no sign-in" json config 'd["applicationServer"]["port"] is None and d["login"] is False'
if json config '"home" in d'; then
	home=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["home"])' "$tmp/config.b")
	status=$(get home "${site[@]}" "${tls[@]}" "$url$home")
	check "  its home, $home, answers the page ($status)" [ "$status" = 200 ]
	check "    as html" header home 'content-type: text/html'
else
	echo "config.json names no home yet:  the empty path is the cards"
fi

#--- a visitor ---------------------------------------------------------------------------------------------------------
status=$(get settings "${site[@]}" "${tls[@]}" "$url/serverSettings")
guest=$(sed -n 's/^authorization: *//p' "$tmp/settings.h" | head -1)
check "a call with no session is given one ($status)" [ -n "$guest" ]
check "  which acts as the guest" json settings 'd["active"] is True'
if [ -n "$guest" ]; then
	status=$(ql connections "$guest" 'serverConnections{ id }')
	check "the guest reads the connections ($status)" json connections '"serverConnections" in d["data"]'
	#users, sessions and logs, which Viewer reads and a visitor must not;  flows, a table no role but the owner's names, which a
	#start would open to everyone were no right held on it;  and a write
	for refused in 'users{ id }' 'sessions{ id }' 'logs{ id }' 'flows{ id }' 'mutation createUser( slug:"smoke", name:"smoke" ){ id }'; do
		status=$(ql refused "$guest" "$refused")
		check "the guest is refused $refused ($status)" [ "$status" = 403 ]
	done
fi

#--- the hub itself, as the owner --------------------------------------------------------------------------------------
if [ -n "$hub" ]; then
	status=$(get hubPage "$hub/")
	check "the hub answers the page itself ($status)" [ "$status" = 200 ]
	status=$(get hubConfig "$hub/config.json")
	check "  with no config.json, which is nginx's alone ($status)" [ "$status" = 404 ]
	owner=${JDE_SESSION:-}; seeded=0
	if [ -z "$owner" ] && [ "$(id -u)" = 0 ] && [ -x "$here/seed.sh" ]; then
		owner=$(JDE_HUB=$hub "$here/seed.sh" session) && seeded=1 || owner=
	fi
	if [ -n "$owner" ]; then
		status=$(ql hubUsers "$owner" 'users{ id }')
		check "the owner reads users ($status)" json hubUsers 'len(d["data"]["users"])>0'
		[ $seeded = 0 ] || curl -s -o /dev/null --max-time 30 -X POST "$hub/logout" -H "Authorization: $owner"
	else
		fail "no owner's session:  set JDE_SESSION to a signed-in browser's, or run as root on the box"
	fi
fi

[ $failed = 0 ] && echo "SMOKE PASS" || echo "SMOKE FAIL"
exit $failed
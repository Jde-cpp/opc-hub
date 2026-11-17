#!/usr/bin/env bash
# The public demo's smoke (docs/demo/plan.md Phases 8 to 10):  what a visitor's browser meets at the public name, the plant as a
# visitor reads it, the gateway's socket as the floor page uses it, smoke.py's half, and with --hub what the owner meets at the
# hub itself.  Run it after every deploy;  the watch's workflow runs it with --quick (.github/workflows/demo-watch.yml).
#   smoke.sh [--url <site>] [--resolve <address>] [--insecure] [--hub <hub>] [--quick]
#     --url        the site                                   default: https://opchub.trypoply.com
#     --resolve    dial the site's name at this address:  127.0.0.1 on the box, or before the DNS records exist
#     --insecure   take any certificate - a container's self-signed pair.  TLS is then not checked, and the run says so
#     --hub        the hub, not through nginx:  http://127.0.0.1:1967 on the box, or the tunnel's end.  The owner's
#                  session is $JDE_SESSION, a signed-in browser's, else as root on the box the seed's (seed.sh session)
#     --quick      the watch's:  the certificate, the page and the socket's half alone, in seconds, with no wait on the plant
# Each check is a line, `ok` or `FAILED`;  the exit is 0 when none failed.  It needs curl and python3, and for the socket's
# half apt's python3-websockets and python3-protobuf, with proto/ beside this file, the demo tarball's.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
url=https://opchub.trypoply.com; resolve=; insecure=0; hub=; quick=0
while [ $# -gt 0 ]; do
	case "$1" in
		--url) url=${2:?}; shift 2;;          --url=*) url=${1#*=}; shift;;
		--resolve) resolve=${2:?}; shift 2;;  --resolve=*) resolve=${1#*=}; shift;;
		--insecure) insecure=1; shift;;
		--hub) hub=${2:?}; shift 2;;          --hub=*) hub=${1#*=}; shift;;
		--quick) quick=1; shift;;
		-h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		*) echo "smoke.sh: unknown option '$1'" >&2; exit 2;;
	esac
done
[ $quick = 0 ] || [ -z "$hub" ] || { echo "smoke.sh: --quick takes no --hub" >&2; exit 2; }
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
json(){ python3 -c 'import json,re,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if eval(sys.argv[2]) else 1)' "$tmp/$1.b" "$2" 2>/dev/null; } #name, a python test of `d`
pick(){ python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' "$tmp/$1.b" "$2" 2>/dev/null; } #name, a python expression of `d`
stamp(){ python3 -c 'import sys,time; print(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time()-int(sys.argv[1]))))' "$1"; } #the time that many seconds ago
ql(){ #name session query - a POST to the site's /graphql, or to the hub's where name starts with hub
	local name=$1 session=$2 base=$url; shift 2
	local args=( "${site[@]}" "${tls[@]}" )
	case "$name" in hub*) base=$hub; args=();; esac
	python3 -c 'import json,sys; print(json.dumps({"query": sys.argv[1]}))' "$1" > "$tmp/$name.q"
	get "$name" "${args[@]}" -X POST "$base/graphql" -H 'Content-Type: application/json' -H "Authorization: $session" --data-binary "@$tmp/$name.q"
}
#The socket's half, smoke.py beside this file, which prints its own checks:  a guest's socket as the floor page uses it.
socket(){
	local args=( --url "$url" )
	[ -z "$resolve" ] || args+=( --resolve "$resolve" )
	[ $insecure = 0 ] || args+=( --insecure )
	python3 "$here/smoke.py" "${args[@]}" || failed=1
}
finish(){
	[ $failed = 0 ] && echo "SMOKE PASS" || echo "SMOKE FAIL"
	exit $failed
}

#--- the front ---------------------------------------------------------------------------------------------------------
if [ $insecure = 1 ]; then
	echo "skipped the certificate's check (--insecure)"
else
	check "TLS:  $url's certificate is valid for $host" [ "$(get tls "${site[@]}" "$url/")" = 200 ]
fi
status=$(get page "${site[@]}" "${tls[@]}" "$url/")
check "the page answers ($status)" [ "$status" = 200 ]
check "  as html" header page 'content-type: text/html'
if [ $quick = 1 ]; then
	socket
	finish
fi
status=$(get redirect "${site[@]}" "http://$host/any?x=1")
check "port 80 redirects to https ($status)" [ "$status" = 301 ]
check "  to the same path on https://$host" header redirect "location: https://$host/any?x=1"
check "  Strict-Transport-Security" header page 'strict-transport-security: max-age='
check "  X-Frame-Options DENY" header page 'x-frame-options: deny'
check "  a Content-Security-Policy, enforced or report-only" header page 'content-security-policy'

#--- config.json -------------------------------------------------------------------------------------------------------
status=$(get config "${site[@]}" "${tls[@]}" "$url/config.json")
check "config.json answers ($status)" [ "$status" = 200 ]
check "  as json" header config 'content-type: application/json'
check "  no-cache" header config 'cache-control: no-cache'
check "  the page's own port, and no sign-in" json config 'd["applicationServer"]["port"] is None and d["login"] is False'
check "  no Access and no Applications section" json config '{"access", "apps"}<=set(d.get("closed", []))'
home=$(pick config 'd["home"]')
check "  its home is the plant's floor (${home:-none})" json config 're.fullmatch( r"/apps/gateways/[^/]+/lines/plant/floor", d["home"] )'
if [ -n "$home" ]; then
	status=$(get home "${site[@]}" "${tls[@]}" "$url$home")
	check "    which answers the page ($status)" [ "$status" = 200 ]
	check "    as html" header home 'content-type: text/html'
fi

#--- the charts --------------------------------------------------------------------------------------------------------
#Highcharts Stock, which the OEE pages' charts and the trend import from the site:  highcharts.sh lays it on the box.  Without it
#each of those pages says the chart is not installed, and the browser's console shows the 404.
status=$(get charts "${site[@]}" "${tls[@]}" "$url/addons/highcharts/esm/highstock.js")
check "the charts' add-on, Highcharts, answers ($status)" [ "$status" = 200 ]
[ "$status" = 200 ] || echo "          on the box, once its licence is held:  sudo /opt/jde-cpp/demo/highcharts.sh"
check "  as javascript" header charts 'content-type: text/javascript'

#--- a visitor ---------------------------------------------------------------------------------------------------------
status=$(get settings "${site[@]}" "${tls[@]}" "$url/serverSettings")
guest=$(sed -n 's/^authorization: *//p' "$tmp/settings.h" | head -1)
check "a call with no session is given one ($status)" [ -n "$guest" ]
check "  which acts as the guest" json settings 'd["active"] is True'
if [ -n "$guest" ]; then
	status=$(ql connections "$guest" 'serverConnections{ id slug connectionStatus{ name } }')
	check "the guest reads the connections, each with whether it is up ($status)" json connections 'all( c["connectionStatus"]["name"] in ("Connected", "Idle", "Error") for c in d["data"]["serverConnections"] )'
	check "  the flow server's not among them, the demo running no flows" json connections 'all( c["slug"]!="flow-server" for c in d["data"]["serverConnections"] )'
	status=$(ql registry "$guest" 'connections( deleted:null ){ instanceId instanceName programName }')
	check "the guest finds the hub's instance, which its pages look up ($status)" json registry 'len(d["data"]["connections"])>0'
	#users, sessions and logs, which Viewer reads and a visitor must not;  a connection's sessions and its last error, the gateway
	#sessions' own;  a process's host, pid, start, past runs and status, and the hosts, the instances' own;  flows, a table no
	#role but the owner's names;  and a write
	for refused in 'users{ id }' 'sessions{ id }' 'logs{ id }' 'serverConnections{ id opcSessions{ count } }' 'serverConnections{ id connectionStatus{ error } }' 'connections{ id hostName pid created }' 'connections{ id deleted }' 'status{ memory startTime }' 'hosts{ id name }' 'flows{ id }' 'mutation createUser( slug:"smoke", name:"smoke" ){ id }'; do
		status=$(ql refused "$guest" "$refused")
		check "the guest is refused $refused ($status)" [ "$status" = 403 ]
	done
	status=$(get gateways "${site[@]}" "${tls[@]}" -H "Authorization: $guest" "$url/opcGateways")
	check "the guest finds the gateways ($status)" json gateways 'len(d["servers"])>0'
	check "  with no pid or start" json gateways 'all( "pid" not in s and "startTime" not in s for s in d["servers"] )'
	[ -z "$home" ] || check "  config.json's home names one of them" json gateways "'$home'.split( '/' )[3] in { s['instanceName'] for s in d['servers'] }"

	#--- the plant, as a visitor reads it ----------------------------------------------------------------------------------
	now=$(stamp 0); day=$(stamp 86400)
	status=$(ql plant "$guest" 'oeeLines{ id slug } oeeMachines{ id slug histGroupId } oeeReasons{ id }')
	check "the guest reads the plant:  the site, its two lines, five machines and the reasons ($status)" json plant '{"plant", "bottling", "packing"}<={ l["slug"] for l in d["data"]["oeeLines"] } and len(d["data"]["oeeMachines"])==5 and len(d["data"]["oeeReasons"])>0'
	if json plant 'len(d["data"]["oeeMachines"])>0 and len(d["data"]["oeeReasons"])>0'; then
		for line in bottling packing; do
			id=$(pick plant "next( (l['id'] for l in d['data']['oeeLines'] if l['slug']=='$line'), 0 )")
			status=$(ql figures "$guest" "oee( line:$id, start:\"$day\", end:\"$now\", by:\"shift\" ){ shift planned run total oee machines }")
			check "the guest reads $line's figures by shift ($status)" json figures 'len(d["data"]["oee"])>0 and all( b["machines"]>0 for b in d["data"]["oee"] )'
		done
		for machine in $(pick plant "' '.join( f'{m[\"slug\"]}:{m[\"id\"]}:{m[\"histGroupId\"]}' for m in d['data']['oeeMachines'] )"); do
			IFS=: read -r slug id group <<<"$machine"
			#its count's node, the member its Total signal reads, as GraphQL:  {nsu:"urn:jde:demo",i:6002}
			status=$(ql count "$guest" "oeeMachineSignals( oeeMachineId:$id ){ role histGroupNodeId } histGroupNodes( histGroupId:$group ){ id node }")
			node=$(pick count "(lambda n: '{'+','.join(f'{k}:{json.dumps(v)}' for k, v in n.items())+'}')( next( json.loads(m['node']) if isinstance(m['node'], str) else m['node'] for s in d['data']['oeeMachineSignals'] if s['role']=='Total' for m in d['data']['histGroupNodes'] if m['id']==s['histGroupNodeId'] ) )")
			check "the $slug's count is a member of its group ($status)" [ -n "$node" ]
			[ -n "${counted:-}" ] || counted=$node #the first machine's, which the history edit below names
			#An engine starts a few seconds after its machine's last edit, and after the hub's start.  After that start the collector is
			#back on each PLC within the minute, so a group made just before it holds nothing until then.
			for _ in $(seq 12); do
				status=$(ql engine "$guest" "oeeMachineStatus( machine:$id ){ engine state }")
				held=$(ql history "$guest" "history( group:$group, nodes:[${node:-{\}}], start:\"$day\", end:\"$(stamp 0)\", limit:1 ){ values{ source } }")
				json engine 'd["data"]["oeeMachineStatus"]["engine"]=="live"' && json history 'len(d["data"]["history"]["values"])>0' && break
				sleep 5
			done
			check "the $slug's engine is live, the machine $(pick engine 'd["data"]["oeeMachineStatus"]["state"]') ($status)" json engine 'd["data"]["oeeMachineStatus"]["engine"]=="live"'
			check "  and its group holds its count's history ($held)" json history 'len(d["data"]["history"]["values"])>0'
		done
		#a stop's coding and a history edit:  each passes while its resource is unenforced, and the seed enforces them.  At a time
		#before any machine's `from`, so a refusal that regressed writes nothing an engine reads.
		id=$(pick plant 'd["data"]["oeeMachines"][0]["id"]'); group=$(pick plant 'd["data"]["oeeMachines"][0]["histGroupId"]'); reason=$(pick plant 'd["data"]["oeeReasons"][0]["id"]')
		status=$(ql refused "$guest" "mutation codeOee( machine:$id, start:\"2000-01-01T00:00:00Z\", end:\"2000-01-01T00:01:00Z\", reason:$reason ){ id }")
		check "the guest is refused codeOee ($status)" [ "$status" = 403 ]
		status=$(ql refused "$guest" "mutation createHistory( group:$group, values:[{ node:${counted:-{\}}, source:\"2000-01-01T00:00:00Z\", value:0 }] ){ values{ status } }")
		check "the guest is refused createHistory ($status)" [ "$status" = 403 ]
		status=$(ql rights "$guest" 'oeeFloorRights{ createFloor updateFloor createShape updateShape purgeShape }')
		check "the floor offers the guest no Edit ($status)" json rights 'not any( d["data"]["oeeFloorRights"].values() )'
	fi
fi

#--- the socket, as the floor page uses it -----------------------------------------------------------------------------
socket

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
		connected='{"plc1", "plc2", "plc3", "plc4", "plc5"}<={ c["slug"] for c in d["data"]["serverConnections"] if c["connectionStatus"]["name"]=="Connected" }'
		for _ in $(seq 12); do #after the hub's restart a PLC refuses the collector until its own login is back, and the collector asks again in 30 s
			status=$(ql hubPlcs "$owner" 'serverConnections{ slug connectionStatus{ name } }')
			json hubPlcs "$connected" && break
			sleep 5
		done
		check "the five PLCs' connections are connected ($status)" json hubPlcs "$connected"
		status=$(ql hubRights "$owner" 'oeeFloorRights{ updateShape }')
		check "the floor offers the owner Edit ($status)" json hubRights 'd["data"]["oeeFloorRights"]["updateShape"] is True'
		[ $seeded = 0 ] || curl -s -o /dev/null --max-time 30 -X POST "$hub/logout" -H "Authorization: $owner"
	else
		fail "no owner's session:  set JDE_SESSION to a signed-in browser's, or run as root on the box"
	fi
fi

finish
#!/usr/bin/env bash
# Launch and drive the Jde C++ services (AppServer, OpcGateway, OpcServer) - or the
# hub (Jde.Opc.Hub = AppServer + OpcGateway in one process; never beside those two).
#
# Each service is one long-lived process holding a web-server port, so the
# handle an agent needs is: start it detached, wait for the port, POST GraphQL
# at it, read its log, kill it.  That is what this script is.
#
#   driver.sh start appserver gateway     # dependency-ordered, waits for each port
#   driver.sh status
#   driver.sh ql appserver 'connections{id programName instanceId}'
#   driver.sh login                       # a web session on the PLC emulator's cert; ql sends it from then on
#   driver.sh ql gateway 'node(opc:"local", id:{ns:5, i:6022}){ value }'   # an OPC read end to end
#   driver.sh logout
#   driver.sh logs gateway 40
#   driver.sh stop all
#   driver.sh smoke                       # full push round trip, self-checking
#
# Everything is relative to $JDE_DIR; build type defaults to debug (JDE_BUILD_TYPE
# overrides).  Services are started with setsid so they outlive this shell.
set -uo pipefail

: "${JDE_DIR:?set JDE_DIR (e.g. /home/duffyj/code/jde/opc-hub)}"
: "${JDE_BUILD_DIR:?set JDE_BUILD_DIR (e.g. /mnt/ram/linux)}"
: "${JDE_COMPILER:?set JDE_COMPILER (e.g. clang++)}"

BUILD_TYPE=${JDE_BUILD_TYPE:-debug}
BUILD_DIR=$JDE_BUILD_DIR/$JDE_COMPILER/$(basename "$JDE_DIR")/$BUILD_TYPE
RUNTIME=$BUILD_DIR/runtime
STATE=$RUNTIME/.run-services
LOGS=$RUNTIME/logs
mkdir -p "$STATE" "$LOGS"

# name|relative exe|relative settings|http port|log file
services(){ cat <<'EOF'
appserver|apps/AppServer/exe/Jde.App.Server|apps/AppServer/config/App.Server.jsonnet|1967|App.Server.log
gateway|apps/OpcGateway/exe/Jde.Opc.Gateway|apps/OpcGateway/config/Opc.Gateway.jsonnet|1968|Opc.Gateway.log
opcserver|apps/OpcServer/exe/Jde.Opc.Server|apps/OpcServer/config/Opc.Server.jsonnet|1970|Opc.Server.log
hub|apps/OpcHub/exe/Jde.Opc.Hub|apps/OpcHub/config/Opc.Hub.jsonnet|1967|Opc.Hub.log
opcserver-hub|apps/OpcServer/exe/Jde.Opc.Server|apps/OpcServer/config/Opc.Server.Hub.jsonnet|1970|Opc.Server.log
opcserver-emulator|apps/OpcServer/exe/Jde.Opc.Server|apps/OpcServer/config/Opc.Server.Emulator.jsonnet|1970|Opc.Server.log
opcserver-emulator-hub|apps/OpcServer/exe/Jde.Opc.Server|apps/OpcServer/config/Opc.Server.Emulator.Hub.jsonnet|1970|Opc.Server.log
EOF
}
# Is <port> held by a Jde.Opc.Hub process?  (it holds 1967 - both roles behind one listener.)
hubHolds(){ local pid; pid=$(pidOn "$1"); [ -n "$pid" ] && ps -p "$pid" -o comm= 2>/dev/null | grep -q '^Jde.Opc.Hub'; }
field(){ services | grep "^$1|" | cut -d'|' -f"$2"; }        # field <service> <n>
exeOf(){ echo "$BUILD_DIR/$(field "$1" 2)"; }
cfgOf(){ echo "$JDE_DIR/$(field "$1" 3)"; }
portOf(){ field "$1" 4; }
logOf(){ echo "$LOGS/$(field "$1" 5)"; }
known(){ services | cut -d'|' -f1 | grep -qx "$1"; }

portBusy(){ ss -lnt 2>/dev/null | grep -q ":$1 "; }
# The pid comes from the listening socket, never from $!: setsid forks, so the pid
# this shell sees exits immediately while the service keeps running under a new one.
pidOn(){ ss -lntp 2>/dev/null | grep ":$1 " | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2; }
waitPort(){ # waitPort <port> <seconds>
	for _ in $(seq "$((${2}*4))"); do portBusy "$1" && return 0; sleep .25; done
	return 1
}

start(){
	local svc=$1 port exe cfg
	known "$svc" || { echo "unknown service '$svc'"; return 2; }
	port=$(portOf "$svc"); exe=$(exeOf "$svc"); cfg=$(cfgOf "$svc")
	[ -x "$exe" ] || { echo "no binary at $exe - build it first (see SKILL.md)"; return 1; }
	if portBusy "$port"; then echo "$svc: already listening on $port"; return 0; fi
	# No -c here.  Console mode keeps a handle on the caller's stdout, so a service
	# started from `driver.sh smoke | tail` holds that pipe open forever and the
	# pipeline never finishes even though the work is done - redirecting stdin and
	# stdout does not shake it loose.  Detached, -c buys nothing: `driver.sh logs`
	# reads the file sink.  Use -c for the terminal path in SKILL.md.
	# -tests: binds the jsonnet ext vars (buildTarget from the binary's own build type,
	#   cwd, logsDir=<cwd>/logs, windows).  It is not a test mode - nothing else in the
	#   process reads it.  It does NOT pick an args dir: every config imports
	#   'args.libsonnet' through -include (relative to the settings file's directory),
	#   so JDE_ARGS_INCLUDE (default args/mysql) is mandatory.
	# setsid: the service must outlive the shell that launched it.  cwd is runtime/ -
	#   logsDir and every relative config path resolve against it.
	# REPO_BUILD_DIR is the build dir's PARENT, not the build dir.
	( cd "$RUNTIME" && \
		REPO_SOURCE_DIR=$JDE_DIR \
		REPO_BUILD_DIR=$JDE_BUILD_DIR/$JDE_COMPILER/$(basename "$JDE_DIR") \
		setsid nohup "$exe" -tests -settings="$cfg" -include="${JDE_ARGS_INCLUDE:-args/mysql}" \
			< /dev/null > "$STATE/$svc.out" 2>&1 & )
	if waitPort "$port" 30; then
		echo "$svc: listening on $port (pid $(pidOn "$port"))"
	else
		echo "$svc: FAILED to bind $port within 30s - stdout:"; tail -20 "$STATE/$svc.out"
		echo "--- log: $(logOf "$svc") ---"; tail -20 "$(logOf "$svc")" 2>/dev/null
		return 1
	fi
}

# Kills whatever holds the service's port - including a process this driver did not
# start.  That is deliberate: a stale service is exactly what blocks the next start.
stop(){
	local svc=$1 pid
	[ "$svc" = all ] && { for s in $(services|cut -d'|' -f1); do stop "$s"; done; return; }
	local port; port=$(portOf "$svc")
	pid=$(pidOn "$port")
	if [ -z "$pid" ]; then echo "$svc: not running"; return 0; fi
	case $svc in appserver|gateway) if hubHolds "$port"; then echo "$svc: port $port is held by the hub (pid $pid) - use 'stop hub'"; return 1; fi;; esac
	kill "$pid" 2>/dev/null
	for _ in $(seq 40); do portBusy "$port" || break; sleep .25; done
	# SIGTERM is not always enough: a service wedged in shutdown keeps the port, and the next
	# ctest run then sits on "Address already in use" for the full 300s timeout instead.
	if portBusy "$port"; then
		kill -9 "$pid" 2>/dev/null
		for _ in $(seq 20); do portBusy "$port" || break; sleep .25; done
	fi
	if portBusy "$port"; then
		echo "$svc: STILL holding $port (pid $(pidOn "$port"))"
		return 1
	fi
	echo "$svc: stopped (pid $pid)"
}

status(){
	printf '%-10s %-6s %-8s %s\n' SERVICE PORT STATE PID
	for s in $(services|cut -d'|' -f1); do
		local p; p=$(portOf "$s")
		printf '%-10s %-6s %-8s %s\n' "$s" "$p" \
			"$(portBusy "$p" && echo up || echo down)" "$(pidOn "$p")"
	done
}

# GraphQL over http.  Mutations go in the same "query" field.  Single-quote the
# argument so the embedded double quotes survive the shell.
# With a session (`login` below, or JDE_SESSION=<hex>) the request carries it as the
# Authorization header - the web session the gateway turns into an OPC credential.
# Without one the POST is anonymous:  fine for `connections`/`logSetting`, but any
# OPC read comes back BadIdentityTokenRejected (the OpcServer refuses an anonymous
# UA session), which is what `login` is for.
SESSION_FILE=$STATE/session
session(){ [ -n "${JDE_SESSION:-}" ] && { echo "$JDE_SESSION"; return; }; [ -f "$SESSION_FILE" ] && cat "$SESSION_FILE"; }
ql(){
	local svc=$1 q=$2 port sid
	known "$svc" || { echo "unknown service '$svc'"; return 2; }
	port=$(portOf "$svc"); sid=$(session)
	python3 -c 'import json,sys; print(json.dumps({"query": sys.argv[1]}))' "$q" \
		| curl -s -X POST "http://localhost:$port/graphql" -H "Content-Type: application/json" \
			${sid:+-H "Authorization: $sid"} --data-binary @-
	echo
}

# A web session for ql - the thing that turns an anonymous POST into an OPC read.
# The gateway derives its OPC credential from the session's user (ConnectAwait.cpp
# SessionCredential:  issued token = the session id, plus the user's pk), so the
# session has to belong to a user with rights on the nodes.  The AppServer's /login
# takes the same certificate jwt the apps log in with (libs/app/client/appClient.cpp
# getJwt):  RS256 over {iat, host, sub:0, name, slug, x5c}, signed by the cert's
# private key, the cert itself carried as x5c and chained against
# /access/trustedCertDirs.  The PLC emulator's login cert is the default - user
# "PlcEmulator.debug.webServer" (enrolled from the CN), which -grant gave
# Read|Update|Subscribe on the pump nodes - so a read through here is the device's
# own view.  Any enrolled cert works:  login [cert.pem] [key.pem].
# The session id (hex) is cached for ql; logout drops it.  Sessions expire on their own.
mintJwt(){ # mintJwt <cert.pem> <key.pem>  -> the compact jwt on stdout
	python3 - "$1" "$2" <<'PY'
import base64, json, re, subprocess, sys, time
cert, key = sys.argv[1], sys.argv[2]
def b64(b): return base64.urlsafe_b64encode(b).rstrip(b'=').decode()
der = subprocess.check_output(['openssl', 'x509', '-in', cert, '-outform', 'DER'])
subject = subprocess.check_output(['openssl', 'x509', '-in', cert, '-noout', '-subject', '-nameopt', 'RFC2253']).decode()
cn = re.search(r'CN=([^,]+)', subject).group(1)
san = subprocess.run(['openssl', 'x509', '-in', cert, '-noout', '-ext', 'subjectAltName'], capture_output=True, text=True).stdout
email = re.search(r'email:([^,\s]+)', san)
name = email.group(1) if email else cn   # enrollment derives the user name UPN -> email -> CN; no UPN here
head = b64(json.dumps({'alg': 'RS256', 'typ': 'JWT'}, separators=(',', ':')).encode())
body = b64(json.dumps({'iat': int(time.time()), 'host': '', 'sub': 0, 'name': name, 'slug': cn, 'x5c': b64(der)}, separators=(',', ':')).encode())
sig = subprocess.check_output(['openssl', 'dgst', '-sha256', '-sign', key, '-binary'], input=f'{head}.{body}'.encode())
print(f'{head}.{body}.{b64(sig)}')
PY
}
login(){
	local certs=${JDE_CERT_DIR:-$HOME/.config/Jde-Cpp/PlcEmulator/ssl}
	local cert=${1:-$certs/certs/PlcEmulator.debug.webServer.PlcEmulator.pem} key=${2:-$certs/private/PlcEmulator.debug.webServer.pem}
	[ -f "$cert" ] && [ -f "$key" ] || { echo "no cert/key at $cert / $key - run the emulator with -createCert, or pass a pair"; return 1; }
	local port; port=$(portOf appserver)
	portBusy "$port" || { echo "nothing listening on $port - start appserver (or hub) first"; return 1; }
	local jwt sid; jwt=$(mintJwt "$cert" "$key") || return 1
	# The session id comes back in the response's Authorization header (LoginAwait::Execute reads the same one).
	sid=$(curl -s -D - -o /dev/null -X POST "http://localhost:$port/login" -H "Authorization: Bearer $jwt" | tr -d '\r' | awk 'tolower($1)=="authorization:"{print $2}')
	[ -n "$sid" ] || { echo "login failed - no Authorization header in the response (is the cert's dir in the AppServer's /access/trustedCertDirs?)"; return 1; }
	echo "$sid" > "$SESSION_FILE"
	echo "session $sid ($(openssl x509 -in "$cert" -noout -subject -nameopt RFC2253 | sed 's/^subject=//')) - ql sends it until logout"
}
logout(){
	local sid; sid=$(session)
	[ -n "$sid" ] || { echo "no session"; return 0; }
	# The gateway's /logout drops its credential cache for the session and purges it on the AppServer.
	local port; port=$(portOf gateway)
	portBusy "$port" && curl -s -o /dev/null -X POST "http://localhost:$port/logout" -H "Authorization: $sid"
	rm -f "$SESSION_FILE"
	echo "session $sid dropped"
}

logs(){ tail -n "${2:-30}" "$(logOf "$1")"; }

# End-to-end check of the log-level push: change a level on the app server and
# read it back out of the gateway's *live* logger, not the table.
smoke(){
	start appserver || return 1
	start gateway || return 1
	local instance before after
	instance=$(ql appserver 'connections{id programName instanceId}' \
		| python3 -c 'import json,sys; print(next(c["instanceId"] for c in json.load(sys.stdin)["data"]["connections"] if c["programName"]=="Jde.OpcGateway"))')
	echo "gateway instance pk: $instance"
	before=$(ql gateway 'logSetting{text}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["data"]["logSetting"]["text"].get("test"))')
	ql appserver "mutation updateInstanceTagLevel( \"id\":$instance, \"text\":[{tags:[\"test\"],level:\"Critical\"}] )" >/dev/null
	sleep 1
	after=$(ql gateway 'logSetting{text}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["data"]["logSetting"]["text"].get("test"))')
	echo "gateway live text.test: $before -> $after"
	ql appserver "mutation updateInstanceTagLevel( \"id\":$instance, \"text\":[{tags:[\"test\"],level:null}] )" >/dev/null
	echo "pushes received by the gateway:"; grep "ClientQuery: size='updateLogSetting" "$(logOf gateway)" | tail -3
	[ "$after" = Critical ] && echo "SMOKE PASS" || { echo "SMOKE FAIL - expected Critical"; return 1; }
}
# The hub: one process, one registration, and /opcGateways answered from it.  The
# level set through the AppServer role lands on the process's own live logger
# (InstanceTagLevelMAwait's self branch runs updateLogSetting in-process) - logSetting{}
# needs a session to read it back, so the proof is that in-process QL trace line.
# Jde.Opc.Hub.Tests covers the read-back through the gateway role with a minted session.
smokeHub(){
	start hub || return 1
	local instance gateways result
	instance=$(ql hub 'connections{id programName instanceId}' \
		| python3 -c 'import json,sys; print(next(c["instanceId"] for c in json.load(sys.stdin)["data"]["connections"] if c["programName"]=="Jde.OpcHub"))')
	echo "hub instance pk: $instance"
	gateways=$(curl -s http://localhost:1967/opcGateways); echo "opcGateways: $gateways"
	echo "$gateways" | grep -q '"port":' || { echo "SMOKE FAIL - /opcGateways lists no gateway (the local instance did not register)"; return 1; }
	result=$(ql hub "mutation updateInstanceTagLevel( \"id\":$instance, \"text\":[{tags:[\"test\"],level:\"Critical\"}] )")
	echo "updateInstanceTagLevel: $result"
	sleep 1
	ql hub "mutation updateInstanceTagLevel( \"id\":$instance, \"text\":[{tags:[\"test\"],level:null}] )" >/dev/null
	echo "$result" | grep -q '"errors"\|error' && { echo "SMOKE FAIL - the mutation errored"; return 1; }
	grep -q 'updateLogSetting("text":{"test":"Critical"}' "$(logOf hub)" && echo "SMOKE PASS" || { echo "SMOKE FAIL - the level never reached the process's own logger (no in-process updateLogSetting in the hub log)"; return 1; }
}

cmd=${1:-status}; shift 2>/dev/null
case $cmd in
	start) for s in "$@"; do start "$s" || exit 1; done;;
	stop) for s in "${@:-all}"; do stop "$s"; done;;
	status) status;;
	ql) ql "$1" "$2";;
	login) login "$@";;
	logout) logout;;
	logs) logs "$1" "${2:-30}";;
	smoke) if [ "${1:-}" = hub ]; then smokeHub; else smoke; fi;;
	*) sed -n '2,20p' "$0"; exit 2;;
esac

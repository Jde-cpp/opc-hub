---
name: run-services
description: Build, launch, and drive the Jde C++ services — AppServer (1967), OpcGateway (1968), OpcServer (1970), or the hub Jde.Opc.Hub (AppServer + OpcGateway in one process on 1967) — the PLC emulator that feeds the OpcServer, and the Open Protocol simulator and bench controller the driver's hand checks run against. Use when asked to run, start, stop, or smoke-test a service, to send it a GraphQL query or mutation, to read a running service's live log levels, or to confirm a change works in the real app rather than in the test suite.
---

# Running the Jde C++ services

Three long-lived servers that register with each other over websockets: **AppServer**
(1967) is the registry, **OpcGateway** (1968) and **OpcServer** (1970) are its clients.
Each is one process holding an HTTP + websocket port, so the handle you need is:
start detached, wait for the port, POST GraphQL at it, read its log, kill it.

**Jde.Opc.Hub** (`apps/OpcHub`) is the AppServer and the OpcGateway in *one* process on
*one* port, 1967: the app routes and app-protocol socket at `/`, the gateway's REST and
OPC socket at `/opc`, one `/graphql` over all three schemas, the gateway wired in-process
(no login/socket to itself).  `hub` in the table below; start it *instead of*
`appserver`+`gateway`, never beside them.
The OpcServer then needs `opcserver-hub` (same exe, `Opc.Server.Hub.jsonnet`), which
anchors the hub's cert for its login TLS - the stock config anchors the split AppServer's.
For the PLC emulator's default (Part 14) transport the OpcServer needs `opcserver-emulator`
instead - `opcserver-emulator-hub` against the hub - see *PLC emulator* below.

`driver.sh` is that handle. Paths below are relative to `$JDE_DIR`; the driver itself is at
`.claude/skills/run-services/driver.sh`.

## Prerequisites

- `$JDE_DIR`, `$JDE_BUILD_DIR`, `$JDE_COMPILER` exported (see the `cpp-build-linux` skill's environment table).
- **MySQL running** on 3306 with the `debug` schema — the services connect as
  `jde@localhost:3306/debug` at startup and abort if it is down. `ss -lnt | grep 3306`.
- `python3` and `curl` (used by the driver for JSON and HTTP).

## Build

```bash
source $JDE_DIR/build/buildFunctions.sh
buildDir=$JDE_BUILD_DIR/$JDE_COMPILER/$(basename $JDE_DIR)/debug
cmake --build $buildDir -j
```

`reconfig $buildDir $JDE_DIR linux-clang-debug-jde` first if the build dir is new —
it creates `runtime/logs`, which the services use as their working directory.

## Run (agent path)

```bash
D=$JDE_DIR/.claude/skills/run-services/driver.sh

$D start appserver gateway      # dependency order matters, see Gotchas
$D start hub opcserver-hub      # the one-process alternative to appserver+gateway (+ the OpcServer against it)
$D status
$D ql appserver 'connections{id programName instanceName instanceId}'
$D logs gateway 40
$D stop all
```

`smoke` does the whole round trip and checks itself — start both services, push a
log level from the AppServer, read it back out of the gateway's live logger:

```bash
$D smoke        # the split stack
$D smoke hub    # the hub: one registration row, /opcGateways answered from the process itself, the level landing on its own logger
```

```
appserver: listening on 1967 (pid 546497)
gateway: listening on 1968 (pid 546568)
gateway instance pk: 3
gateway live text.test: Trace -> Critical
pushes received by the gateway:
deb-19:40:42.871 [1.1]ClientQuery: size='updateLogSetting(text: {"test":"Critical"}, persist: false){}'.
SMOKE PASS
```

### Driving with GraphQL

`ql <service> '<query>'` POSTs to that service's `/graphql`. Mutations go in the
same field — single-quote the argument so the embedded double quotes survive:

```bash
$D ql appserver 'mutation updateInstanceTagLevel( "id":3, "text":[{tags:["sql"],level:"Debug"}] )'
```

Two queries worth knowing:

| query | against | what it tells you |
|---|---|---|
| `connections{id programName instanceName instanceId}` | appserver | who is registered, and each one's **instance pk** — the id every per-instance mutation takes |
| `logSetting{text binary appServer}` | any service | that process's **live** logger state, not the table. This is how you prove a runtime change actually landed |

`instanceTagLevel(id:N){text binary appServer}` against the AppServer reads the
*persisted* levels for instance N — compare it with the target's own `logSetting`
to tell "written" apart from "applied".

### Reading an OPC value: log in first

An anonymous `ql gateway` cannot read a node — the gateway opens the UA session with
the *web session's* credential, and with no session there is none, so the OpcServer
answers `BadIdentityTokenRejected`. `login` fixes that: it mints the same certificate
jwt the apps present to the AppServer's `/login` (RS256 over `{iat, host, sub, name,
slug, x5c}` — `appClient.cpp` `getJwt`), gets a session back, and every `ql` from
then on sends it as the `Authorization` header. The default identity is the PLC
emulator's login cert (`~/.config/Jde-Cpp/PlcEmulator/ssl`, user
`PlcEmulator.debug.webServer`, `Read|Update|Subscribe` on the pump nodes from
`-grant`); `login <cert.pem> <key.pem>` uses another enrolled cert.

```bash
$D login
$D ql gateway 'serverConnections{name slug}'                          # the opc: argument below
$D ql gateway 'namespaces(opc:"local"){ index uri }'                     # urn:jde:pumps is ns 5 on this server
$D ql gateway 'node(opc:"local", id:{ns:5, i:6022}){ value }'            # pump2.motorRpm, end to end
$D ql gateway 'nodes(opc:"local", id:[{ns:5,i:6012},{ns:5,i:6022}]){ id value }'   # several at once
$D logout
```

`id` is an object argument (`NodeId::ParseQL` reads it as one), the shape the SPA's
`gateway-service.ts` uses; `variable(opc:…, ns:5, i:6022){ value }` parses but answers
an empty `{}`.

`JDE_SESSION=<hex>` overrides the cached session (e.g. one copied from a browser).

That is the CLI path. For the *SPA* the login is the browser's, not the driver's: with the
claude-in-chrome tools navigate to `http://localhost:4200/login` first and let Google Identity
Services auto-select the profile's account — see `web/CLAUDE.md` § *Verifying in the browser*.

## Run (human path)

Same binaries, foreground, logging to the terminal. Export the two env vars, `cd` to
`runtime/` (that is where the logs land), then one line per service:

```bash
export REPO_BUILD_DIR=$JDE_BUILD_DIR/$JDE_COMPILER/$(basename $JDE_DIR)
export REPO_SOURCE_DIR=$JDE_DIR
cd $REPO_BUILD_DIR/debug/runtime

$REPO_BUILD_DIR/debug/apps/AppServer/exe/Jde.App.Server   -c -tests -settings=$REPO_SOURCE_DIR/apps/AppServer/config/App.Server.jsonnet -include=args/mysql
$REPO_BUILD_DIR/debug/apps/OpcServer/exe/Jde.Opc.Server   -c -tests -settings=$REPO_SOURCE_DIR/apps/OpcServer/config/Opc.Server.jsonnet -include=args/mysql
$REPO_BUILD_DIR/debug/apps/OpcGateway/exe/Jde.Opc.Gateway -c -tests -settings=$REPO_SOURCE_DIR/apps/OpcGateway/config/Opc.Gateway.jsonnet -include=args/mysql
# or the hub instead of the AppServer + gateway lines:
$REPO_BUILD_DIR/debug/apps/OpcHub/exe/Jde.Opc.Hub         -c -tests -settings=$REPO_SOURCE_DIR/apps/OpcHub/config/Opc.Hub.jsonnet -include=args/mysql
```

Ctrl-C to stop. `-c` is console mode; `-tests` binds the ext vars and `-include=args/mysql`
resolves `import 'args.libsonnet'` - both are needed (see Gotchas); `args/sqlite -arg path=<file>`
on a box without MySQL.

## PLC emulator

The shippable doc is `apps/OpcServer/emulator/README.md`; this section is the agent's run path.
`apps/OpcServer/emulator` → `Jde.Opc.PlcEmulator`: a stand-in for a UA-enabled PLC that
drives the OpcServer's pump tags (`apps/OpcServer/config/nodesets/pumps.NodeSet2.xml`).
It runs its own UA server (loopback 4842, the UA server library's secured endpoint, sessions
logged in through the hub) holding that nodeset and **publishes** the
pump process values over OPC UA PubSub (UADP/UDP, contract in
`apps/OpcServer/config/pubsub/pumps.libsonnet` — the OpcServer's `/opcServer/pubsub`
reader imports the same file), and holds a client session on the OpcServer for the
`status` run commands the web UI writes. `-transport=write` routes every tag over that
session instead (no PLC server) — the way to exercise plain client writes.
`-transport=server` runs the PLC server alone, no OpcServer session and no writer: a
gateway `server_connections` row on `opc.tcp://127.0.0.1:4842` with `certificateUri`
`urn:jde:plc-emulator` collects from it as from a PLC (the dev gateway config trusts
`Jde-Cpp/PlcEmulator/ssl/certs`; the emulator's `plc.trustedCertDirs` trusts the
gateway's and the hub's), and a `status` written on the served node is a write-through
the emulator logs `(external, user N)`. A *group* on that connection needs the hub's
collector a login the PLC resolves: the default is the hub's own certificate, which is no
hub user, so set `gateway.hist.credentials.<slug>: {user, password}` on the hub and the
same `plc.users` entry on the emulator - both hidden defaults, so an overlay needs `:::`
(a plain `:` over a `::` key stays hidden, and the setting silently never applies).

**The OpcServer needs `opcserver-emulator`, not `opcserver`, for the pubsub transport.**
A DataSetReader writes its targets server-internally — no session, no `OpcAuthorize` — so
the stock `Opc.Server.jsonnet` ships with no `/opcServer/pubsub` key and no reader; the
`Opc.Server.Emulator.jsonnet` overlay (`Opc.Server.Emulator.Hub.jsonnet` against a hub)
adds it, and `PubSub::Reader` WARNs `is UNAUTHENTICATED` at startup when it does. Started
as plain `opcserver`, everything looks healthy and the emulator's samples land nowhere:
its log still says `published=…`, the OpcServer's has no `PubSub reader:` line.
`-transport=write` needs no overlay.

It has no HTTP port, so it is not in `driver.sh`'s service table; run it foreground.
First run only — cert, grant, restart (the OpcServer loads acls at startup):

```bash
E=$REPO_BUILD_DIR/debug/apps/OpcServer/emulator/Jde.Opc.PlcEmulator
C=$REPO_SOURCE_DIR/apps/OpcServer/emulator/config/Opc.PlcEmulator.jsonnet
$D start appserver opcserver-emulator  # OpcServer's first boot registers the nodeIds resource
cd $REPO_BUILD_DIR/debug/runtime
$E -c -tests -settings=$C -createCert   # login cert + UA cert under Jde-Cpp/PlcEmulator/ssl (a trustedCertDirs entry)
$E -c -tests -settings=$C -grant        # createAcl: Read|Update|Subscribe on opc.<buildTarget>/nodeIds for the login cert's user
$D stop opcserver-emulator && $D start opcserver-emulator # loads the acl
$E -c -tests -settings=$C               # runs until Ctrl-C (or -duration=PT10M); -transport=write for session writes
```

Expected in its log: `PLC server on 127.0.0.1:4842 with 'pumps.NodeSet2.xml';  publishing dataSet 'pumps' @ opc.udp://…`,
`Client security policies: …None, …Basic256Sha256, applicationUri filter: 'urn:open62541.server.application'`,
`Session activated`, `Connected … 3 command tag(s) subscribed`, then every `statusPeriod`
a `cycles=… published=… writes=… externalChanges=… reconnects=…` line. In the OpcServer's
log: `PubSub 'pumps' field 'pump1.motorRpm' (Double) -> ns=N;i=6012` ×5, `PubSub reader: …`
and the `PubSub reader on '…' is UNAUTHENTICATED` warning that says the overlay is in force.

Watching it: in the web UI open the OpcServer connection → `Objects` → `pump2`, subscribe
`motorRpm` and `status` (sine + a 15 s toggle); on `pump1` write `status=false` → the
emulator logs `[pump1]status <- false (external)` and `motorRpm` decays with τ=3 s.
`$D stop opcserver-emulator; $D start opcserver-emulator` while it runs → `reconnecting
(#1) in PT1S`, then the subscribe lines again.

Notes: the PubSub url is multicast (`224.0.0.22`) — a box without a multicast route
needs the unicast form `opc.udp://127.0.0.1:<port>/` in *both* configs (the test config
already uses it). Plaintext UDP: this open62541 build has no SKS. Tags are the nodeset
now — change the XML and restart OpcServer (and the emulator, which loads the same file).
The old `.mutation` loader and the `createObject`/`createObjectType`/`createConstructor`
hooks are gone; those GraphQL mutations now degrade to plain table inserts with no UA
side effect.

## Open Protocol simulator

Jarrekstar's open-protocol-device-simulator, the hand check of the Open Protocol plan's
Phase 10a (`docs/open-protocol/plan.md`, *Phase 0* 0.3's *Built*): a clone at
`$REPO_DIR/open-protocol-device-simulator` pinned at `7e604da0`, never in the superbuild
or on a runner. Build once, with Node and cargo on PATH; the npm and svelte-kit steps are
what its README lacks at that commit:

```bash
git clone https://github.com/Jarrekstar/open-protocol-device-simulator.git $REPO_DIR/open-protocol-device-simulator
cd $REPO_DIR/open-protocol-device-simulator
git checkout 7e604da0
npm --prefix frontend ci --replace-registry-host=always   # its lock resolves one package from a private feed
( cd frontend && npx svelte-kit sync )                     # tsconfig.json extends .svelte-kit/tsconfig.json, which the cargo build's own sync writes elsewhere
cargo build --release                                      # a few minutes; embeds the web UI
```

Run it on loopback at the protocol's port, its HTTP API on 8081; it writes `simulator.db`
in the cwd (`--db-path` moves it) and is foreground, so it is not in `driver.sh`'s table:

```bash
cd $REPO_DIR/open-protocol-device-simulator
./target/release/open-protocol-device-simulator --bind-address 127.0.0.1 --tcp-port 4545 --http-port 8081
```

Expected: `Open Protocol TCP server listening on 127.0.0.1:4545`. A gateway row against it
is `open-protocol://127.0.0.1:4545`; `--config <toml>` with `[webui] enabled = true` serves
its UI on 8082 (`config.example.toml` lists the keys). The smoke client
`python3 .claude/skills/run-services/open-protocol-smoke.py [tcp] [http]` opens a session
stepping down from MID 0001 at 6, subscribes from MID 0060 at 7, posts one tightening, acks
the MID 0061 and stops, printing each frame and `SMOKE PASS`. It reads each frame's MID and
revision and decodes nothing, so a pass says the simulator answers, not that the codec reads
it.

The driver's check against it is four cases of `Jde.Opc.Tests`, `DriverSimulatorTests`
(the plan's Phase 10a *Built*), skipped unless `/testing/openProtocolSimulator` names the
simulator's two ports. That key and the suite's filter are hidden in the suite's config, so
a scratch config beside it sets both with `:::`. A case makes its own rows, lifts the
simulator's profile itself and puts it back:

```bash
C=$JDE_DIR/apps/OpcGateway/tests/config/simulator.jsonnet
echo "(import 'Opc.Tests.jsonnet') + { testing+:{ tests::: 'DriverSimulatorTests.*', openProtocolSimulator::: { port: 4545, httpPort: 8081 } } }" > $C
cd $buildDir/runtime
REPO_SOURCE_DIR=$JDE_DIR REPO_BUILD_DIR=$(dirname $buildDir) $buildDir/apps/OpcGateway/tests/Jde.Opc.Tests -tests -settings=$C -include=args/sqlite -arg path=:memory:
rm $C
```

Gotchas:

- **Each MID family answers the revision its profile selects**, 1 by default, or any it
  implements, and MID 0004 code 97 to the rest, so a client steps down to 1 - which is the
  step-down's check, not a fault. `GET /protocol/profile`, set a family's `policy` to
  `any_implemented` or its `revision`, and `PUT` it back whole with the `version` it came
  with; `GET /protocol/catalog` lists the families and what each implements (MID 0001 to
  6; MID 0061 1 to 7, 998, 999).
- **The profile outlives a restart:** it is kept in the database, so delete `simulator.db`
  or pass a fresh `--db-path` to start from the default.
- **MIDs 0060 to 0063 are one family**, so the tightening family pinned above 1 refuses
  the driver's MID 0062, which goes at 1, with a MID 0004 after every result. Leave it at
  `any_implemented` and pick MID 0061's revision with the row's ceiling, `{"0061": N}`.
- **Where it is not the tables'** (the plan's Phase 10a *Built*), none a fault of the run.
  Its MID 0002 at 4 to 6 has fields 08 to 16 at widths that are not the tables' (the RBU
  type at 19 for 24 first), so the codec refuses it and the row never connects: keep MID
  0002 at 3, by the profile or the row's `driver`, `{"revisions":{"0002":3}}`. Its MID 0004
  and MID 0005 carry the revision of the request they answer (`00240005007` to a MID 0060
  at 7), which the codec reads all the same. A subscription it refuses is followed by its
  push at revision 1, which the session acknowledges.
- **A command answers by its mode:** MID 0038 only in Job mode (`POST /config/operation-mode`
  with `{"mode": "job"}`, and a job to select, `POST /jobs`), code 20 outside it, and MID
  0018 only outside it, code 3 inside.
- **Driving it:** `POST /simulate/tightening` with `{"torque": 12.5, "angle": 40.0, "ok": true}`
  makes one result, `/auto-tightening/start` a run, `/psets/{id}/select` and
  `/jobs/{id}/select` the selections, `/config/failure` latency, loss and corruption.
- **It has no** MID 0064, 0070 to 0077, 0080, 9997 or 9998, and its MID 0002 at 6 says no
  sequence numbers, so link level is off: backfill, alarms, the clock and link level are
  the bench controller's (10b, *Open Protocol bench controller* below).

## Open Protocol bench controller

The hand checks of the Open Protocol plan's Phase 10b (`docs/open-protocol/plan.md`, *Phase 10*):
a Power Focus 6000 with two virtual stations, each on its own port, the tool at the first.
Three tools, in this order, and what each cannot see the next does.  Until the controller is
there every one of them runs against the library's stand-in for it (`StandIn.h`), which is
how they were checked; a pass there says the tool works, not what the controller does.

**The controller**, per station in its *Protocols* menu: *Open Protocol* On, *Server port
number* 4545 and 4546, *Communication timeout* 15 s, *Disconnect* None, *PLC Index* 1 and 2,
and both ports open in its firewall.  Note its firmware release: it goes in every label
below.  A command takes more (PF6000 appendix §2.5, §2.7): MID 0018 a *Source Tightening*
task, and its number is that task's *Identifier number*, not the set's index, which is what
MID 0015 and a result carry; MID 0038 a *Source Batch* with numbers.

**1. The bench client**, `open-protocol-bench.py`, speaks the protocol raw, reads it by the
specification's tables and never through the codec, so it shows what the controller sends
whatever the codec makes of it, and makes the checks the driver cannot: a withheld
acknowledgement, a number out of order.  Every frame in is printed, logged to
`<out>/<label>.log` and kept as a file, up to `--per` (4) of each MID and revision.  A mode
a check, each station in turn (`-h` lists them):

```bash
P=$JDE_DIR/.claude/skills/run-services/open-protocol-bench.py
H=192.168.0.10; O=~/pf6000-capture; L=pf6000-3.10-s1     # the controller, where the capture goes, what sent it
python3 $P identify $H 4545 --out $O --label $L   # MID 0002 field by field:  sequence number support, the station id
python3 $P ladder   $H 4545 --out $O --label $L   # the step-down:  each request from the driver's revision, the refusals on the way
python3 $P link     $H 4545 --out $O --label $L   # link level:  a request acknowledged, the same again;  --break adds a number out of order
python3 $P clock    $H 4545 --out $O --label $L   # MID 0081 against this box's clock
python3 $P first    $H 4545 --out $O --label $L   # what follows each subscription's MID 0005 with nothing changed
python3 $P results  $H 4545 --out $O --label $L   # make 3 tightenings:  each MID 0061 by its fields, a name's bytes past ASCII in hex
python3 $P walk     $H 4545 --out $O --label $L   # MID 0064 back from the latest:  ids, times, stations
python3 $P alarms   $H 4545 --out $O --label $L   # raise an alarm, acknowledge it:  MID 0071, 0074, 0076
python3 $P hold     $H 4545 --out $O --label $L   # make 3 tightenings:  what comes while MID 0062 is withheld, and after it
python3 $P hold     $H 4545 --out $O --label $L --level link   # the same with MID 9997 withheld
python3 $P ask      $H 4545 60 9 --out $O --label $L           # anything else:  a MID, a revision, its data
```

What the appendix's Table 1 has `ladder` print for a PF6000: MID 0060 answered at 7, MID 0014
at 1 behind refusals at 3 and 2, MID 0034 at 4 behind 6 and 5, MID 0051 at 2, MID 0070 at 3,
MID 0064 at 7 and MID 0080 at 1.  A `!!` line is where a frame leaves its table, the byte
and the parameter named.  `--spaces` writes a header's unused fields blank, as the
specification's examples, where the default is the digits the driver's encoder writes: a
controller that answers one and not the other is a finding.

**2. The driver's cases**, four of `Jde.Opc.Tests`, `DriverControllerTests`, skipped unless
`/testing/openProtocolController` names the controller: the step-down, link level, the
identity and the clock of each station; a result landing in its station's row alone; an
alarm raised and acknowledged; and an outage filled by the walk across both stations, the
link cut by a relay of the case's own.  A case asks on the console for what it needs of the
hand at the controller, `>>>>>>>>>> At the controller:  make 2 tightening(s) at station 1`,
waits `handTimeout` (10 min) for it, and prints under `NOTE` what the specification leaves
open.  The last one has the tool moved to station 2 and leaves it there.  The key and the
filter are hidden in the suite's config, so a scratch config beside it sets both with `:::`;
`log` and the tag put every frame in the suite's log, and `maxBackfill` is the suite's 6
raised for a controller with a history:

```bash
C=$JDE_DIR/apps/OpcGateway/tests/config/bench.jsonnet
echo "(import 'Opc.Tests.jsonnet') + { testing+:{ tests::: 'DriverControllerTests.*', openProtocolController::: { host: '$H', stations: [4545, 4546] } }, gateway+:{ drivers+:{ openProtocol+:{ log: true, maxBackfill: 2000 } } }, logging+:{ spd+:{ tags+:{ openProtocol: 'Trace' } } } }" > $C
cd $buildDir/runtime
REPO_SOURCE_DIR=$JDE_DIR REPO_BUILD_DIR=$(dirname $buildDir) $buildDir/apps/OpcGateway/tests/Jde.Opc.Tests -tests -settings=$C -include=args/sqlite -arg path=:memory: 2>&1 | tee $O/cases.log
rm $C
```

`timeZone: 'Europe/Stockholm'` beside `host` is the zone the controller's clock keeps where
it is not this box's, and `psetName: 'Größe M8'` the name its results are to carry, a set
named outside ASCII at the controller first.  `openProtocolController::: { standIn: true }`
in place of the host runs the four against the stand-in, about 30 s, the cases' own check.
A case that fails against the controller is a difference from the specification's account:
settle it against the tables, fix the codec or the driver where they are wrong, and fold
what the controller does into `docs/open-protocol/spec.md`.

**3. The hub**, for what takes a day or a hand on the HMI.  A dev database made before the
row had its `kind` stops the hub at `Unknown column 'kind'`: sync its schema, `-sync` on one
start, or run the hub on a scratch sqlite file, `-include=args/sqlite -arg path=<file> -sync`,
which is how these lines were checked.  A row a station and a group a row, which no page
makes yet; a mutation refused for want of rights takes a browser session's `JDE_SESSION`:

```bash
$D start hub && $D login
$D ql hub 'mutation createServerConnection( slug:"pf6000-s1", name:"PF6000 station 1", isDefault:false, url:"open-protocol://192.168.0.10:4545", kind:"open-protocol" ){id}'
$D ql hub 'mutation createHistGroup( name:"PF6000 station 1", slug:"pf6000-s1", serverConnectionId:<id> ){id}'
$D ql hub 'mutation createHistGroupNode( histGroupId:<group>, node:"{\"nsu\":\"urn:jde:pf6000-s1:open-protocol\",\"s\":\"Result.TighteningId\"}" ){id}'   # and Counters.Total, Result.Torque
$D ql hub 'nodes(opc:"pf6000-s1", id:[{ns:2,s:"_Op.Connected"},{ns:2,s:"_Op.Revisions"},{ns:2,s:"_Op.Results"},{ns:2,s:"_Op.LostResults"},{ns:2,s:"_Op.Reconnects"},{ns:2,s:"_Op.LastError"}]){ id value }'
$D ql hub 'history( group:<group>, nodes:[{nsu:"urn:jde:pf6000-s1:open-protocol",s:"Result.TighteningId"}], start:"2026-11-14T00:00:00Z", end:"2026-11-15T00:00:00Z", limit:1000 ){ continuation values{ source status value } }'
```

A row whose controller keeps another zone takes `driver:"{\"timeZone\":\"Europe/Stockholm\"}"`.
`/gateway/drivers/openProtocol/log: true` with the `openProtocol` tag at Trace, set in a
config beside `Opc.Hub.jsonnet`, puts every frame in `Opc.Hub.log`.

- **The Disconnect action.**  At the HMI set station 1's *Disconnect* to lock the tool, then
  `$D stop hub`: try the trigger, and write down what the HMI shows, whether the second
  station's tool is locked too, and what frees it, the hub's return or a hand at the HMI.
  Set it back to None.  The help's warning is written from this.
- **The soak.**  A day of tightenings at both stations with the hub up, a cable pulled once
  or twice.  At its end each row's `_Op.Results` is its group's count of `Result.TighteningId`
  over the day, `_Op.LostResults` is 0, `_Op.Reconnects` the cables pulled, and the bench
  client's `walk --steps 50` at each station names the ids the group holds, in its order.

**The captures** become the codec's vectors: `python3 $P frames $buildDir/runtime/logs/Opc.Hub.log --out $O --label $L`
takes the frames a log holds, as the client kept its own; a few of each MID and revision go
into `libs/opc/openProtocol/tests/fixtures/` (its `README.md` says which), and
`Jde.Opc.OpenProtocol.Tests`' `FixtureTests` then reads each.

**Before the controller**, the stand-in in place of it.  The client's modes against
`StandInTests.DISABLED_Serves`, which serves its two stations on 14545 and 14546 for five
minutes (`/testing/standIn`), a rundown a second, an alarm every twenty:

```bash
cd $buildDir/runtime
REPO_SOURCE_DIR=$JDE_DIR REPO_BUILD_DIR=$(dirname $buildDir) $buildDir/libs/opc/openProtocol/tests/Jde.Opc.OpenProtocol.Tests -tests -settings=$JDE_DIR/libs/opc/openProtocol/tests/config/OpenProtocol.Tests.jsonnet --gtest_also_run_disabled_tests --gtest_filter=StandInTests.DISABLED_Serves &
python3 $P walk 127.0.0.1 14545 --out /tmp/capture --label standin-s1
```

Gotchas:

- **The stand-in is the appendix, not the controller.**  It numbers its messages, pushes
  what is selected as a subscription is accepted and has the revisions of Table 1.  Whether
  a PF6000 does the first two is what the bench is for: Table 1 has no row for MID 9997 or
  9998 and marks MID 0002 partly supported at revision 6.
- **The appendix on ServAid is release 2.8's**, valid from 2.8.14.  A controller on a later
  release may answer higher revisions than its table, which the step-down's check then shows.
- **A loosening is a result.**  A bolt loosened between two tightenings comes as MID 0061 too,
  result type 2: it counts in `_Op.Results` and a watcher's ids, and not in `Counters`.
- **A second client.**  The outage's case holds a session of its own at each station beside
  the row's.  A station that refuses or drops it has no slot for one, which is the finding,
  and the case cannot run there.
- **The served stand-in's results are loosenings** at both stations at once, so against it
  by `host` the result case fails on its counters and on the other row, and the rest pass.

## Test

`ctest` from the build dir, per the **cpp-tests** skill. **Stop the services first, and
check they are actually down**: `Jde.Opc.Server.Tests` and `Jde.Opc.Tests` stand up their
own AppServer *and* gateway on 1967 and 1968, and a leftover on either burns the full
450s timeout on `(62)system - Address already in use`.

```bash
$D stop all && $D status      # every service must read down
cd $buildDir && ctest --timeout 300
```

## Gotchas

- **`-tests` is not a test mode — it is what makes the config resolve, and a normal
  run needs it.** The app configs read `std.extVar("buildTarget")` / `("logsDir")` and
  `import 'args.libsonnet'`, which only exists under `config/args/<dialect>/`. `-tests`
  (and `-ctest`) binds those ext vars — `buildTarget` from the binary's own build type,
  `cwd`, `logsDir=<cwd>/logs`, `windows`. Nothing else in the process branches on it.
  It does **not** pick an import dir: `-include=<dir>` (relative to the settings file's
  directory) is what resolves `args.libsonnet`, so every launch carries both — the driver
  passes `-include=${JDE_ARGS_INCLUDE:-args/mysql}`. Without `-tests`, startup dies at
  *"RUNTIME ERROR: undefined external variable: buildTarget"*; without `-include`, at
  *"couldn't open import 'args.libsonnet'"* - either way the settings object becomes
  `{"error":…}`. `-arg buildTarget=debug -arg logsDir=…` in place of `-tests` works too,
  and is what you need if you ever want a `buildTarget` that isn't the binary's own.
- **`logsDir` is `<cwd>/logs`, so the working directory decides where logs land.**
  Run from `runtime/`; run from somewhere else and the logs follow you there.
- **Don't add `-c` to a detached launch.** Console mode keeps a handle on the caller's
  stdout, so a service started from inside a pipeline (`driver.sh smoke | tail`) holds
  that pipe open forever — the pipeline hangs long after the work finished, and neither
  `< /dev/null` nor `timeout` on the driver shakes it loose. `-c` is for the terminal
  path, where you want the log in front of you. The driver omits it and reads the file
  sink via `logs`.
- **`REPO_BUILD_DIR` is the build dir's PARENT**, not the build dir:
  `$JDE_BUILD_DIR/$JDE_COMPILER/$(basename $JDE_DIR)`. Point it at the build dir and
  the driver `.so`s resolve to `…/debug/debug/…` and fail to load.
- **Start the AppServer first** — or the hub, never both. OpcServer and OpcGateway call
  it over HTTP during startup; if it is not listening, OpcServer logs
  `(6f)system - Connection refused`, runs its shutdown functions, and exits *after*
  having briefly bound 1970 — so a `ss | grep 1970` a second too early looks like
  success. Against the hub the OpcServer must run as `opcserver-hub` (the
  `Opc.Server.Hub.jsonnet` overlay anchors the hub's `OpcHub.pem`), else its login TLS
  fails on the hub's cert; the PLC emulator likewise takes `Opc.PlcEmulator.Hub.jsonnet`,
  and its OpcServer `opcserver-emulator-hub` (hub anchor + the Part 14 reader).
  `stop appserver`/`stop gateway` refuse a port the hub holds — `stop hub`.
- **The log files are truncate-on-open.** A second launch of the same service wipes
  the first one's `runtime/logs/<App>.log`, including a launch that immediately died
  on `Address already in use`. If a log starts mid-story, something relaunched.
- **`$!` is not the service's pid.** `setsid` forks, so the pid the launching shell
  sees exits immediately. The driver reads the pid off the listening socket
  (`ss -lntp`) instead — and `stop` therefore kills whatever holds the port, not just
  what the driver started.
- **Instance pks are per program and stable across restarts; instance *names* are not
  unique.** AppServer and OpcGateway both register as "Debug" (OpcServer names itself
  `OpcServer.mysql.debug`), so only the pk distinguishes them — 1, 3 and 2 respectively
  in this database. Always read them back from `connections{}` rather than hard-coding,
  and never key off the name alone. The hub registers **once**, as `Jde.OpcHub` /
  `OpcHub.debug` (its own pk; no `Jde.OpcGateway` row) — `/opcGateways` still lists the
  gateway role (host:1967, from a local registration rather than a socket), and `$D ql
  gateway …` is meaningless against a hub: `$D ql hub` answers the gateway's queries too.
- **`instanceTagLevel` answers grouped by level, while the mutation takes tag→level.**
  The query gives `{"text":{"Debug":["sql",["socket","client","read"]],"Information":["default"]}}`
  — the tags are values, and a combined tag is an array, because as an *object key* it
  serializes to `["socket","client","read"]`, which is not a tag name and never parses
  back. The mutation carries tags as values for the same reason — a record per override:
  `mutation updateInstanceTagLevel("id":3,"text":[{tags:["socket","client","read"],level:"Debug"},{tags:["crypto"],level:null}])`,
  where a null (or absent) level deletes. `tags:["socket.client.read"]` works too — **`.`
  is the separator** (`Jde::TagSeparator`), in config keys and live `logSetting` names as
  well. It was `_` until 2026-08-12; an old spelling now logs `Unknown tag` and resolves
  to nothing, which for a config key means the override silently does not apply.

## Troubleshooting

| symptom | fix |
|---|---|
| `RUNTIME ERROR: undefined external variable: buildTarget` | add `-tests` |
| `Failed to evaluate … couldn't open import 'args.libsonnet'` | add `-tests` (or `-include=args/mysql`) |
| `cri … (62)system - Address already in use` then the process exits | something already holds the port: `$D status`, then `$D stop <service>` |
| OpcServer/OpcGateway exits seconds after start, log ends in `Connection refused` | the AppServer is not up — `$D start appserver` first |
| `<svc>: FAILED to bind <port> within 30s` while its log ends at `Web Server started` | the exe is stale against a rebuilt lib: `.run-services/<svc>.out` ends in `symbol lookup error: … undefined symbol`; rebuild that service's exe target, `driver.sh`'s second field: `cmake --build <dir> --target <exe> -j8`, `<exe>` one of `Jde.App.Server`, `Jde.Opc.Gateway`, `Jde.Opc.Server` (every `opcserver*`) or `Jde.Opc.Hub` |
| `mysql::connect_params` then an abort | MySQL is down, or the `debug` schema is missing |
| `$D ql` returns nothing | the service is not listening; `$D status` |
| a gateway connect fails `BadCertificateUntrusted` and the log says `server certificate for 'opc.tcp://…' rejected` | the gateway verifies OPC server certificates (since 2026-09-04, `src/ServerTrust.cpp`): copy the server's `.pem` into a dir listed under `/access/trustedCertDirs` (`Opc.Gateway.jsonnet` seeds `certsDir("OpcServer")`, so a Jde OpcServer on this host is already trusted), or set `gateway.verifyServerCertificate: false`. The same status *without* that log line means the server rejected the gateway's certificate — the OpcServer's `/access/trustedCertDirs` side |
| Opc test suites time out at 450s | something holds a test port — `Jde.Opc.Tests`/`Jde.Opc.Server.Tests` bind the live ports +100 (2067/2068/2070/4940) and `Jde.Opc.Hub.Tests` 1973/1975/4975, so a live service (1967/1968/1970, the emulator's 4842) is not the cause; look for an orphaned test binary (`ss -lntp \| grep -E '2067\|2068\|2070\|4940\|1973\|1975\|4975'`) |
| OpcServer log `certificate verify failed` / `(167772294) … unable to get local issuer certificate` logging in to 1967 | the hub is up and the OpcServer was started with the stock `Opc.Server.jsonnet` — `$D start opcserver-hub` |
| the emulator logs `published=…` climbing but no pump value moves in the UI | the OpcServer has no DataSetReader — it was started as `opcserver` rather than `opcserver-emulator` (or `opcserver-emulator-hub`). Its log has no `PubSub reader:` line; the reader is an overlay because it is an unauthenticated write path |
| `/opcGateways` returns `{"servers":[]}` with the hub up | the in-process gateway did not register its local instance (`hubStartup.cpp` → `App::Server::AddLocalInstance`); the SPA cannot find the gateway |
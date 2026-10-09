---
name: run-services
description: Build, launch, and drive the Jde C++ services — AppServer (1967), OpcGateway (1968), OpcServer (1970), or the hub Jde.Opc.Hub (AppServer + OpcGateway in one process on 1967) — and the PLC emulator that feeds the OpcServer. Use when asked to run, start, stop, or smoke-test a service, to send it a GraphQL query or mutation, to read a running service's live log levels, or to confirm a change works in the real app rather than in the test suite.
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

- `$JDE_DIR`, `$JDE_BUILD_DIR`, `$JDE_COMPILER` exported (see CLAUDE.md).
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
It runs its own headless UA server (4841) holding that nodeset and **publishes** the
pump process values over OPC UA PubSub (UADP/UDP, contract in
`apps/OpcServer/config/pubsub/pumps.libsonnet` — the OpcServer's `/opcServer/pubsub`
reader imports the same file), and holds a client session on the OpcServer for the
`status` run commands the web UI writes. `-transport=write` routes every tag over that
session instead (no PLC server) — the way to exercise plain client writes.

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

Expected in its log: `PLC server up on port 4841 … publishing dataSet 'pumps' @ opc.udp://…`,
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

## Test

`ctest` from the build dir, per the **cpp-tests** skill. **Stop the services first, and
check they are actually down**: `Jde.Opc.Server.Tests` and `Jde.Opc.Tests` stand up their
own AppServer *and* gateway on 1967 and 1968, and a leftover on either burns the full
300s timeout on `(62)system - Address already in use`.

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
| `mysql::connect_params` then an abort | MySQL is down, or the `debug` schema is missing |
| `$D ql` returns nothing | the service is not listening; `$D status` |
| a gateway connect fails `BadCertificateUntrusted` and the log says `server certificate for 'opc.tcp://…' rejected` | the gateway verifies OPC server certificates (since 2026-09-04, `src/ServerTrust.cpp`): copy the server's `.pem` into a dir listed under `/access/trustedCertDirs` (`Opc.Gateway.jsonnet` seeds `certsDir("OpcServer")`, so a Jde OpcServer on this host is already trusted), or set `gateway.verifyServerCertificate: false`. The same status *without* that log line means the server rejected the gateway's certificate — the OpcServer's `/access/trustedCertDirs` side |
| Opc test suites time out at 300s | a service still holds 1967 or 1968 — `$D stop all`, then `$D status` to confirm; a SIGTERM-deaf gateway needs the `kill -9` the driver now escalates to (`Jde.Opc.Hub.Tests` is on 1973-1975 and does not care) |
| OpcServer log `certificate verify failed` / `(167772294) … unable to get local issuer certificate` logging in to 1967 | the hub is up and the OpcServer was started with the stock `Opc.Server.jsonnet` — `$D start opcserver-hub` |
| the emulator logs `published=…` climbing but no pump value moves in the UI | the OpcServer has no DataSetReader — it was started as `opcserver` rather than `opcserver-emulator` (or `opcserver-emulator-hub`). Its log has no `PubSub reader:` line; the reader is an overlay because it is an unauthenticated write path |
| `/opcGateways` returns `{"servers":[]}` with the hub up | the in-process gateway did not register its local instance (`hubStartup.cpp` → `App::Server::AddLocalInstance`); the SPA cannot find the gateway |

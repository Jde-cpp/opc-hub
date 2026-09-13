# Jde.Opc.PlcEmulator

A stand-in for a UA-enabled PLC, feeding the OpcServer's pump tags.  Three things a real device does, in one process:

- **its own OPC UA server** (`PlcServer`) holding the same NodeSet2 the OpcServer loads
  (`apps/OpcServer/config/nodesets/pumps.NodeSet2.xml`), bound to loopback - headless, nothing is meant to connect to it;
- **a Part 14 publisher** (`PubSub::Writer`, UADP over UDP) sampling those local nodes and publishing the contract in
  `apps/OpcServer/config/pubsub/pumps.libsonnet` - the OpcServer's `DataSetReader` imports the same file, so the
  DataSetMetaData both ends build cannot drift;
- **a client session on the OpcServer** (`EmulatorClient`) for the run commands the web UI writes (`status`), subscribed,
  and for any tag the contract does not publish, written.

The process values are generated (`Signals`): `sine`, `ramp`, `randomWalk`, `counter`, `toggle`, and `follow` - a motor
lagging towards `ratedRpm` while the device is commanded on and towards 0 when it is not.  Which tag does what is
`/emulator/devices` in the config.  The device clock keeps running while the OpcServer session is down: a PLC does not
freeze because a client left.

| | value |
|---|---|
| exe / tests | `Jde.Opc.PlcEmulator` / `Jde.Opc.PlcEmulator.Tests` (`tests/`, 25 units, no database or server) |
| `Process::ProductName()` (`$(ProgramData)/Jde-Cpp/<product>`: certs) | `PlcEmulator` |
| settings / log | `config/Opc.PlcEmulator.jsonnet` (`Opc.PlcEmulator.Hub.jsonnet` against a hub) / `Opc.PlcEmulator.log` |
| ports | none listening for HTTP; the PLC's UA endpoint on `127.0.0.1:4841`; UADP to the contract's url |
| identity | AppServer user = the login cert's CN (`PlcEmulator.debug.webServer`); OPC `applicationUri` `urn:jde:plc-emulator` |

## Run

The OpcServer needs the emulator overlay for the default transport - `opcserver-emulator` in the run-services driver
(`Opc.Server.Emulator.jsonnet`; `opcserver-emulator-hub` against a hub).  Started as plain `opcserver` everything looks
healthy and the samples land nowhere: the stock config carries no reader (see *Security* below).  `-transport=write`
needs no overlay.

```bash
D=$JDE_DIR/.claude/skills/run-services/driver.sh
E=$REPO_BUILD_DIR/debug/apps/OpcServer/emulator/Jde.Opc.PlcEmulator
C=$JDE_DIR/apps/OpcServer/emulator/config/Opc.PlcEmulator.jsonnet
$D start appserver opcserver-emulator
cd $REPO_BUILD_DIR/debug/runtime
$E -c -tests -settings=$C                       # until Ctrl-C; -duration=PT10M for a bounded run
$E -c -tests -settings=$C -transport=write      # every tag over the session, no PLC server
```

`-c` is console mode (without it the process detaches); `-tests` binds the jsonnet ext vars (`buildTarget`, `logsDir`) -
it is not a test mode.  Expect in the log: `PLC server up on opc.tcp://127.0.0.1:4841 … publishing dataSet 'pumps'`,
`Connected to 'opc.tcp://127.0.0.1:4840': 3 command tag(s) subscribed`, then every `statusPeriod` a
`cycles=… published=… writes=… writeFailures=… externalChanges=… reconnects=… - pump1[motorRpm=… status=…] …` line.
In the OpcServer's log, `PubSub reader on '…' is UNAUTHENTICATED` says the overlay is in force.

Reading a value back from outside: `$D login` (a web session on this device's login cert), then
`$D ql gateway 'node(opc:"local", id:{ns:5, i:6022}){ value }'` - pump2's motorRpm through the gateway.

### First run

```bash
$E -c -tests -settings=$C -createCert   # the AppServer login cert and the UA channel cert, under Jde-Cpp/PlcEmulator/ssl
$E -c -tests -settings=$C -grant        # once the OpcServer has booted: Read|Update|Subscribe on opc.<buildTarget>/nodeIds for this user
$D stop opcserver-emulator && $D start opcserver-emulator   # the OpcServer loads acls at startup
```

The first login enrolls the user from the login certificate's CN.  `-grant` writes the acl for *that* user through the
AppServer (`createAcl`) - it needs the OpcServer to have registered the `nodeIds` resource, so it runs after the
OpcServer's first boot, and the OpcServer only reads acls when it starts, hence the restart.  A stale acl from an older
build is cleared by deleting its rows (`access_acl`, `access_permission_rights`, `access_permissions`) and granting
again.

## Transports

| `-transport` | process values | commands | OpcServer side |
|---|---|---|---|
| `pubsub` (default) | published: the PLC server's nodes, sampled by the writer every `publishingInterval`, land in the OpcServer's target variables through its reader | subscribed over the session | needs the `Opc.Server.Emulator*` overlay |
| `write` | written over the session, one `Write` per tag per cycle | subscribed over the session | stock config; an ordinary authenticated client |

A tag named in the contract (`<device>.<tag>`) is published; `command` tags are subscribed; everything else is written -
so under `pubsub`, pump2's `status` toggle is still a session write.

## Config

`config/Opc.PlcEmulator.jsonnet`; the keys under `/emulator` and the command-line overrides that beat them:

| key | override | meaning |
|---|---|---|
| `transport` | `-transport=` | `pubsub` \| `write` |
| `url` | `-url=` | the OpcServer endpoint |
| `period` | `-period=` | the device cycle - every generator advances once per period |
| `statusPeriod` | `-statusPeriod=` | the summary line's cadence (the final line at exit is skipped when the timed one just said it) |
| `duration` | `-duration=` | bounded run; unset = until Ctrl-C |
| `reconnectMin` / `reconnectMax` | `-reconnectMin=` / `-reconnectMax=` | the reconnect back-off's first delay and its cap |
| `opcSchema` | `-opcSchema=` | the acl schema `-grant` writes into (`opc.<buildTarget>`) |
| `applicationUri` | | this device's identity: what it advertises and the SAN of its channel certificate |
| `serverApplicationUri` | | the endpoint filter: only endpoints whose server advertises it; `""` takes any |
| `verifyServerCertificate` | | verify the OpcServer's certificate against `/access/trustedCertDirs` before opening the session (default `true`) |
| `plc.port` / `plc.bind` / `plc.nodeset` | | the PLC server's endpoint (`bind: ""` = every interface, and a WARN) and the NodeSet2 it loads |
| `pubsub` | | the contract - `import` the shared file, never a copy |
| `ssl` | | the UA channel certificate's settings (SAN = `applicationUri`; re-issued on drift at start) |
| `devices[]` | | `path` (browse path under Objects, in the contract's namespace by default; `<index>~name` for another) and `tags[]` (`name`, `mode`, `min`/`max`/`step`/`period`/`tau`/`ratedRpm`) |

Durations are ISO 8601 (`PT30S`).  `TagSpec` refuses `max<=min` where a range is used, non-positive `period`/`tau`, and a
non-positive `step` for `randomWalk`/`counter`, at startup, naming the tag.

## Trust

Three certificates, three directions:

| certificate | where | trusted by |
|---|---|---|
| login (`http.ssl`, CN `PlcEmulator.debug.webServer`) | `Jde-Cpp/PlcEmulator/ssl/certs/…webServer.PlcEmulator.pem` | the AppServer: its `/access/trustedCertDirs` lists the `PlcEmulator` dir (the dev configs; an installed hub's `config/args/install` lists only the OpcServer's - add this one there); the CN becomes the user |
| UA channel (`emulator.ssl`, CN `PlcEmulator.opc`, SAN `urn:jde:plc-emulator`) | same dir | the OpcServer: the same `trustedCertDirs` entry (again the dev args; the installed ones need it added); it rescans on a failed verify, so a re-issued cert is picked up |
| the OpcServer's own | `Jde-Cpp/OpcServer/ssl/certs` | this process: `/access/trustedCertDirs` in this config, checked before every session (`verifyServerCertificate`) - the anchors, not the OS root store |

The login TLS handshake with the AppServer is anchored separately (`web.client.ssl.caFile` = the AppServer's own cert;
the hub overlay swaps in the hub's).

## Security

The Part 14 path is a **plaintext, unauthenticated write** into the OpcServer's address space: a `DataSetReader` lands
received fields through the server-internal write - no session, no `OpcAuthorize` - and its only filter is the
`publisherId`/`writerGroupId`/`dataSetWriterId` triple in a tracked config file.  Anyone who can reach the url drives
`pump*.motorRpm`, and the default url is a multicast group (`opc.udp://224.0.0.22:4840/`), i.e. the multicast domain.
This open62541 build has no SKS.  That is why the reader lives in the `Opc.Server.Emulator*` overlays and not in the
stock OpcServer config, and why `PubSub::Reader` WARNs at startup when it is present: a demo affordance, not a production
one.  `-transport=write` has none of this - it is an authenticated session under the acl `-grant` wrote.

The PLC's own server is anonymous-full over a writable nodeset (open62541's `setMinimal`), which is why it binds
loopback; `plc.bind: ""` opens it to the LAN deliberately, with a WARN.

## Network

Multicast needs a route (a box with a default route has one; a bare container may not).  The unicast form
`opc.udp://127.0.0.1:<port>/` works everywhere - set it in the contract, which both ends import, or in an overlay on
each side; the test configs use it.

## Tests

`Jde.Opc.PlcEmulator.Tests` (`tests/`): `Signals` (parsing, every refusal, each generator's shape), `ParseDevices`, and
`PlcServer` on loopback 4851 with the writer aimed at a port nothing reads.  `ctest -R Jde.Opc.PlcEmulator.Tests`.  The
OpcServer suite's `PubSubTests` covers the reader end with an in-process publisher of the same shape.

## Known limits

- The OPC issued token is the AppServer session id, captured when the client is built; after an AppServer (or hub)
  restart the app socket logs in again with a new session, but the OpcServer reconnects keep presenting the old one and
  are refused (`BadIdentityTokenInvalid`) until the emulator is restarted.
- `UA_Client_connect` is synchronous: a failed attempt stalls the device clock for up to the 10 s client timeout.
- Only the `Double`/`Float`/`Boolean` field types are published; the pumps contract is all `Double`.

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
| exe / tests | `Jde.Opc.PlcEmulator` / `Jde.Opc.PlcEmulator.Tests` (`tests/`, 44 units, no database or server) |
| `Process::ProductName()` (`$(ProgramData)/Jde-Cpp/<product>`: certs) | `PlcEmulator` |
| settings / log | [`Opc.PlcEmulator.jsonnet`](../../../apps/OpcServer/emulator/config/Opc.PlcEmulator.jsonnet) ([`Opc.PlcEmulator.Hub.jsonnet`](../../../apps/OpcServer/emulator/config/Opc.PlcEmulator.Hub.jsonnet) against a hub; `Opc.PlcEmulator.Quality[.Hub].jsonnet` adds scheduled status codes) / `Opc.PlcEmulator.log` |
| ports | none listening for HTTP; the PLC's UA endpoint on `127.0.0.1:4841`; UADP to the contract's url |
| identity | AppServer user = the login cert's CN (`PlcEmulator.debug.webServer`); OPC `applicationUri` `urn:jde:plc-emulator` |

## Run

The OpcServer needs the emulator overlay for the default transport - `opcserver-emulator` in the run-services driver
([`Opc.Server.Emulator.jsonnet`](../../../apps/OpcServer/config/Opc.Server.Emulator.jsonnet); `opcserver-emulator-hub` against a hub).  Started as plain `opcserver` everything looks
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

[`Opc.PlcEmulator.jsonnet`](../../../apps/OpcServer/emulator/config/Opc.PlcEmulator.jsonnet); the keys under `/emulator` and the command-line overrides that beat them:

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
| `verifyServerCertificate` | | verify the OpcServer's certificate against `trustedCertDirs` before opening the session (default `true`) |
| `trustedCertDirs` | | the OPC servers this PLC trusts, one `.pem`/`.crt` per server, read on every connect - the emulator's own list (`/emulator/trustedCertDirs`), not the enrollment anchors |
| `plc.port` / `plc.bind` / `plc.nodeset` | | the PLC server's endpoint (`bind: ""` = every interface, and a WARN) and the NodeSet2 it loads |
| `pubsub` | | the contract - `import` the shared file, never a copy |
| `ssl` | | the UA channel certificate's settings (SAN = `applicationUri`; re-issued on drift at start) |
| `devices[]` | | `path` (browse path under Objects, in the contract's namespace by default; `<index>~name` for another) and `tags[]` (`name`, `mode`, `min`/`max`/`step`/`period`/`tau`/`ratedRpm`) |

Durations are ISO 8601 (`PT30S`).  `TagSpec` refuses `max<=min` where a range is used, non-positive `period`/`tau`, and a
non-positive `step` for `randomWalk`/`counter`, at startup, naming the tag.

## Status codes

Every reading carries a StatusCode - its data quality, [OPC 10000-4 §7.38](https://reference.opcfoundation.org/specs/OPC-10000-4/7.38).
**The pubsub transport delivers it**: the PLC server's node holds the whole DataValue, the writer publishes it
DataValue-encoded (`PubSub::Writer`'s `STATUSCODE` field content mask), and the OpcServer's reader writes value and status
into the target variable.  A session write carries it too, but a server only takes a non-Good status from a session with
`StatusWrite` on the node's AccessLevel *and* on the user's ([OPC 10000-3 §8.57](https://reference.opcfoundation.org/specs/OPC-10000-3/v1.05.06/8.57));
the pumps nodeset is AccessLevel 3 and `-grant` stops at `Read|Update|Subscribe`, so the OpcServer answers
`BadWriteNotSupported`, the emulator WARNs once per tag per session and writes the value alone from there - under
`-transport=write` the quality shows in the emulator's status line and nowhere downstream.  A tag is Good unless its config says otherwise, and the stock config says nothing - [`Opc.PlcEmulator.Quality.jsonnet`](../../../apps/OpcServer/emulator/config/Opc.PlcEmulator.Quality.jsonnet)
is the overlay that does - one example of every shape the UI decodes, on a 2 min cycle with one scheduled fault at a time:

```bash
$E -c -tests -settings=$JDE_DIR/apps/OpcServer/emulator/config/Opc.PlcEmulator.Quality.jsonnet   # …Quality.Hub.jsonnet against a hub
```

| into the cycle | tag | status | code |
|---|---|---|---|
| 20-30 s | `pump2.motorRpm` | `UncertainSensorNotAccurate` - the value keeps moving | `0x40930000` |
| 30-35 s | `pumpManual.motorRpm` | `Good+SemanticsChanged` | `0x00004000` |
| 40-55 s | `pump3.motorRpm` | `BadSensorFailure` - held; the ramp runs on underneath | `0x808C0000` |
| 60-75 s | `pump1.motorRpm` | `GoodLocalOverride` - a Good sub-code | `0x00960000` |
| 80-90 s | `pump4.motorRpm` | `Good+Overflow` | `0x00000480` |
| 90-95 s | `pumpManual.motorRpm` | `Good+StructureChanged` | `0x00008000` |
| 100-115 s | `pump4.motorRpm` | `UncertainLastUsableValue+Constant` - held | `0x40900700` |
| every peak and trough | `pump2.motorRpm` | `UncertainEngineeringUnitsExceeded+High` / `+Low` - a sine of 800-1600 read by a transmitter ranged 900-1500 | `0x40940600` / `0x40940500` |

The layout the emulator composes (Tables 176/177):

| bits | field | emulated |
|---|---|---|
| 31:30 | Severity - `00` Good, `01` Uncertain, `10` Bad (`11` reserved, refused) | from `status` |
| 27:16 | SubCode | from `status` |
| 15 / 14 | StructureChanged / SemanticsChanged | `structureChanged` / `semanticsChanged` |
| 11:10 | InfoType - `01` DataValue: the info bits below apply | set whenever a limit or overflow bit is |
| 9:8 | LimitBits - `00` None, `01` Low, `10` High, `11` Constant | `limit`, or the sensor range |
| 7 | Overflow | `overflow` |
| 4:0 | historian bits | no - they describe stored data, not a live reading |

Per tag, beside `mode` (not on a `command` tag - the emulator never writes one):

| key | meaning |
|---|---|
| `sensorMin` / `sensorMax` | the transmitter's range.  A generated value outside it is published pinned at the range as `UncertainEngineeringUnitsExceeded` with LimitBits Low/High.  Not on a bool. |
| `quality[]` | scheduled faults on the tag's own clock (it starts with the emulator); the **first active** window wins, outside them the sensor range decides, else Good |
| `quality[].status` | a name below, or any code by number (`"0x80340000"`, or the JSON number) - the rest of Table 178 |
| `quality[].start` / `duration` / `every` | active from `start` (default 0) for `duration` (required), again every `every` (not shorter than `duration`; absent = once) |
| `quality[].hold` | publish the last reading from before the window - a blind sensor.  Default `true` for Bad and the two `…LastUsableValue` codes, else `false`.  The generator advances either way, so the reading jumps when the window closes. |
| `quality[].limit` | `none` \| `low` \| `high` \| `constant`; absent = the sensor range's limit, if pinned |
| `quality[].overflow` / `structureChanged` / `semanticsChanged` | the flag bits |

Names, spelled as the stack spells them: `Good` `GoodLocalOverride` `GoodClamped` · `Uncertain`
`UncertainNoCommunicationLastUsableValue` `UncertainLastUsableValue` `UncertainSubstituteValue` `UncertainInitialValue`
`UncertainSensorNotAccurate` `UncertainEngineeringUnitsExceeded` `UncertainSubNormal` · `Bad` `BadConfigurationError`
`BadNotConnected` `BadDeviceFailure` `BadSensorFailure` `BadOutOfService` `BadNoCommunication` `BadCommunicationError`
`BadWaitingForInitialData` `BadOutOfRange`.

Downstream, the gateway renders an Uncertain (or flagged Good) reading as `{"v":…,"sc":…}` and a Bad one as `{"sc":…}` -
the code in place of the value (`Value::ToJson`); subscriptions carry `sc` beside the value.  The status line marks a
non-Good tag: `motorRpm=1500.0(UncertainEngineeringUnitsExceeded+High)`.

The web UI words it the same way.  A node's **Children** table marks a reading that is not plain Good with an icon
beside the value (error / warning / info; the name, its flags and the numeric code in the tooltip), and has a **Status**
column - hidden in the default view, switched on in the view editor - that spells it out and takes the icon over.  A Bad row keeps the last value it had, dimmed and locked, until a reading that is not Bad arrives
(`Variable.setReading` in [`node.ts`](../../../web/opc/control/src/lib/model/node.ts), the decoding in [`status-code.ts`](../../../web/opc/control/src/lib/model/status-code.ts)).

## Trust

Three certificates, three directions:

| certificate | where | trusted by |
|---|---|---|
| login (`http.ssl`, CN `PlcEmulator.debug.webServer`) | `Jde-Cpp/PlcEmulator/ssl/certs/…webServer.PlcEmulator.pem` | the AppServer: its `/access/trustedCertDirs` lists the `PlcEmulator` dir (the dev configs; an installed hub's `config/args/install` lists only the OpcServer's - add this one there); the CN becomes the user |
| UA channel (`emulator.ssl`, CN `PlcEmulator.opc`, SAN `urn:jde:plc-emulator`) | same dir | the OpcServer: the same `trustedCertDirs` entry (again the dev args; the installed ones need it added); it rescans on a failed verify, so a re-issued cert is picked up |
| the OpcServer's own | `Jde-Cpp/OpcServer/ssl/certs` | this process: `/emulator/trustedCertDirs` in this config, checked before every session (`verifyServerCertificate`) - the anchors, not the OS root store |

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

`Jde.Opc.PlcEmulator.Tests` (`tests/`): `Signals` (parsing, every refusal, each generator's shape), `Quality` (the
status names, the §7.38 bit layout, the windows, hold and the sensor range), `ParseDevices`, and `PlcServer` on loopback
4851 with the writer aimed at a port nothing reads.  `ctest -R Jde.Opc.PlcEmulator.Tests`.  The OpcServer suite's
`PubSubTests` covers the reader end with an in-process publisher of the same shape - values, and
`PublishedStatusLandsInTargetVariable` for a status crossing the wire.

## Known limits

- The OPC issued token is the AppServer session id, captured when the client is built; after an AppServer (or hub)
  restart the app socket logs in again with a new session, but the OpcServer reconnects keep presenting the old one and
  are refused (`BadIdentityTokenInvalid`) until the emulator is restarted.
- `UA_Client_connect` is synchronous: a failed attempt stalls the device clock for up to the 10 s client timeout.
- Only the `Double`/`Float`/`Boolean` field types are published; the pumps contract is all `Double`.
- Status codes reach the OpcServer over pubsub only - a session-written tag's non-Good status is refused (no
  `StatusWrite`) and its value is written alone.  A Bad reading always carries its last value: the OpcServer's reader
  skips a field without one.  The source timestamp is not held with a held reading, and the historian bits are not
  emulated.

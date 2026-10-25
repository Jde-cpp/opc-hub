# Jde.Opc.PlcEmulator

A stand-in for a UA-enabled PLC, feeding the OpcServer's pump tags, or the gateway directly.  Three things a real
device does, in one process:

- **its own OPC UA server** (`PlcServer`) holding the same NodeSet2 the OpcServer loads
  (`apps/OpcServer/config/nodesets/pumps.NodeSet2.xml`), on the UA server library's endpoint (`Jde.Opc.UAServer`):
  the OpcServer's security policies, a trust list, and sessions logged in through the hub.  Under `-transport=server`
  it is the whole device - a gateway group collects from it as from a real PLC, and the run commands arrive as writes
  to its own nodes;
- **a Part 14 publisher** (`PubSub::Writer`, UADP over UDP) sampling those local nodes and publishing the contract in
  `apps/OpcServer/config/pubsub/pumps.libsonnet` - the OpcServer's `DataSetReader` imports the same file, so the
  DataSetMetaData both ends build cannot drift;
- **a client session on the OpcServer** (`EmulatorClient`) for the run commands the web UI writes (`status`),
  subscribed, and for any tag the contract does not publish, written.

The process values are generated (`Signals`).  The modes are:

- `sine`, `ramp`, `randomWalk`, `counter`, `toggle`;
- `follow`: a motor lagging towards `ratedRpm` while the device is commanded on, and towards 0 when it is not;
- `command`: not generated, subscribed from the OpcServer.

Which tag does what is `/emulator/devices` in the config.

The device clock keeps running while the OpcServer session is down.  A PLC does not freeze because a client left.

| | value |
|---|---|
| exe / tests | `Jde.Opc.PlcEmulator` / `Jde.Opc.PlcEmulator.Tests` (`tests/`, 50 units, no database or server) |
| settings / log | [`Opc.PlcEmulator.jsonnet`](config/Opc.PlcEmulator.jsonnet) ([`Opc.PlcEmulator.Hub.jsonnet`](config/Opc.PlcEmulator.Hub.jsonnet) against a hub; `Opc.PlcEmulator.Quality[.Hub].jsonnet` adds scheduled status codes) / `Opc.PlcEmulator.log` |
| ports | none listening for HTTP; the PLC's UA endpoint on loopback 4842 (`plc.address` `""` for every interface); UADP to the contract's url |
| identity | AppServer user = the login cert's CN (`PlcEmulator.debug.webServer`); OPC `applicationUri` `urn:jde:plc-emulator`, the client's and the PLC server's alike |

## Run

The OpcServer needs the emulator overlay for the default transport,
[`Opc.Server.Emulator.jsonnet`](../config/Opc.Server.Emulator.jsonnet).  The
[run-services driver](../../../.claude/skills/run-services/driver.sh) starts it as `opcserver-emulator`
(`opcserver-emulator-hub` against a hub).

Started as plain `opcserver`, everything looks healthy and the samples land nowhere.  The stock config carries no
reader (see *Security* below).

`-transport=write` needs no overlay.  `-transport=server` needs no OpcServer: the gateway connects to the PLC.

`$REPO_BUILD_DIR` is the parent of the build dir (`$JDE_BUILD_DIR/$JDE_COMPILER/opc-hub`), as the
[root README](../../../README.md#building) exports it.

```bash
D=$JDE_DIR/.claude/skills/run-services/driver.sh
E=$REPO_BUILD_DIR/debug/apps/OpcServer/emulator/Jde.Opc.PlcEmulator
C=$JDE_DIR/apps/OpcServer/emulator/config/Opc.PlcEmulator.jsonnet
$D start appserver opcserver-emulator
cd $REPO_BUILD_DIR/debug/runtime
$E -c -tests -settings=$C                       # until Ctrl-C; -duration=PT10M for a bounded run
$E -c -tests -settings=$C -transport=write      # every tag over the session, no PLC server
$E -c -tests -settings=$C -transport=server     # the PLC server alone on 4842, for a gateway group; no OpcServer session
```

`-c` is console mode (without it the process detaches); `-tests` binds the jsonnet ext vars (`buildTarget`, `logsDir`) -
it is not a test mode.  Expect in the log: `PLC server on 127.0.0.1:4842 with 'pumps.NodeSet2.xml';  publishing dataSet 'pumps' …`,
`Connected to 'opc.tcp://127.0.0.1:4840': 3 command tag(s) subscribed`, then every `statusPeriod` a
`cycles=… published=… writes=… writeFailures=… externalChanges=… reconnects=… - pump1[motorRpm=… status=…] …` line.
In the OpcServer's log, `PubSub reader on '…' is UNAUTHENTICATED` says the overlay is in force.

Reading a value back from outside: `$D login` (a web session on this device's login cert), then
`$D ql gateway 'node(opc:"local", id:{ns:5, i:6022}){ value }'` - pump2's motorRpm through the gateway.

### Server transport

The PLC server is the device.  Expect `PLC server on 127.0.0.1:4842 with 'pumps.NodeSet2.xml'`, then the status
lines with `writes=` counting the local writes and `externalChanges=` the commands clients wrote.  To collect from it:

1. Trust, both ways.  The gateway (hub or standalone) must trust the PLC's certificate - the dev
   `Opc.Gateway.jsonnet` lists `Jde-Cpp/PlcEmulator/ssl/certs` in `gateway.trustedCertDirs`; an installed one needs
   the dir added, or the `.pem` copied into its `ssl/servers`.  The PLC trusts the gateway's per-connection
   certificates through `plc.trustedCertDirs`, which names the gateway's and the hub's product dirs.
2. A `server_connections` row: url `opc.tcp://127.0.0.1:4842`, `certificateUri` `urn:jde:plc-emulator` (the secured
   channel; without it the channel is None and only the token is encrypted).  A gateway on another box needs
   `plc.address` set: `""` for every interface, or the interface it dials.  The collector presents its hub session
   as the issued token; the PLC's app client asks the hub about it, so the hub must be the one this process logged
   into.
3. A template over the pump variables and a group from it, as for any server.  The group's collector logs in with the
   gateway's own certificate by default, which has to be a hub user's key: a split gateway's is, since it logged in with
   it; a hub's own certificate is not, so give that collector a username - `gateway.hist.credentials.<slug>` in the hub's
   config and the same name in `plc.users` (both are hidden keys with their defaults; an overlay un-hides one with
   `:::`, a plain `:` over a `::` key stays hidden).  A `status` written on the gateway's served node goes through to
   the PLC (write-through) and the emulator logs `[pump1]status <- false (external, user N)`.

Every node's value is open to every session - the login is the gate (`plc.tokenTypes`): anonymous is off by default, a
`username` login needs a `plc.users` entry, and a certificate token must be a hub user's key.  No session can add
references or write other attributes.  The emulator writes each generated tag as its node's DataType: Boolean, an integer
(rounded and held to its range), an enumeration, Float or Double, and Double for an abstract one; a tag on a node of
any other type is refused at startup.  A command node's value at startup is where its command starts.

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
| `server` | written to the PLC server's own nodes, status included; no writer | clients' writes to the PLC's own nodes (a value callback) | none - a gateway connection to the PLC instead |

A tag named in the contract (`<device>.<tag>`) is published; `command` tags are subscribed; everything else is written -
so under `pubsub`, pump2's `status` toggle is still a session write.  Under `server` there is no contract and no session:
every tag is a node of the PLC's own server, resolved once at startup.

## Config

[`Opc.PlcEmulator.jsonnet`](config/Opc.PlcEmulator.jsonnet): the keys under `/emulator`, and the command-line
overrides that beat them.

| key | override | meaning |
|---|---|---|
| `transport` | `-transport=` | `pubsub` \| `write` \| `server` |
| `url` | `-url=` | the OpcServer endpoint |
| `period` | `-period=` | the device cycle - every generator advances once per period |
| `statusPeriod` | `-statusPeriod=` | the summary line's cadence (the final line at exit is skipped when the timed one just said it) |
| `duration` | `-duration=` | bounded run; unset = until Ctrl-C |
| `reconnectMin` / `reconnectMax` | `-reconnectMin=` / `-reconnectMax=` | the reconnect back-off's first delay and its cap |
| `opcSchema` | `-opcSchema=` | the acl schema `-grant` writes into (`opc.<buildTarget>`) |
| `applicationUri` | | this device's identity: what it advertises and the SAN of its channel certificate |
| `serverApplicationUri` | | the endpoint filter: only endpoints whose server advertises it; `""` takes any |
| `verifyServerCertificate` | | verify the OpcServer's certificate against `trustedCertDirs` before opening the session (default `true`) |
| `trustedCertDirs` | | the OPC servers this PLC trusts, one certificate per server (`.pem`, `.crt`, `.der` or `.cer`), read on every connect - the emulator's own list (`/emulator/trustedCertDirs`), not the enrollment anchors |
| `plc.port` / `plc.address` / `plc.nodeset` | | the PLC server's endpoint (4842; `address` absent = loopback, `""` = every interface) and the NodeSet2 it loads |
| `plc.ssl` | | the PLC server's certificate; absent = the device's `ssl` pair below, one identity for client and server |
| `plc.trustedCertDirs` / `plc.rejectedCertDir` | | the UA clients admitted (the gateway's and the hub's product dirs, where their per-connection certificates land); where a refused one is written as `<thumbprint>.der` |
| `plc.none` / `plc.tokenTypes` / `plc.users` / `plc.userTokenPolicyUri` | | the UA server library's login settings, as the OpcServer's `/opc` and the gateway's `/gateway/server`: a None endpoint beside the secured ones, which tokens are offered (anonymous off), the username list |
| `plc.limits` | | open62541's `maxSessions`, `maxSecureChannels` (never below `maxSessions`), `maxMonitoredItems`, `sessionTimeout`, `minSamplingInterval`, `maxAsyncOperations`; every gateway user browsing the PLC holds a session beside the collector's |
| `pubsub` | | the contract - `import` the shared file, never a copy |
| `ssl` | | the UA channel certificate's settings (SAN = `applicationUri`; re-issued on drift at start) |
| `namespace` | | the device paths' default namespace; absent = the contract's (`pubsub.dataSet.namespace`), so the write and server transports need no `pubsub` block |
| `devices[]` | | `path` (browse path under Objects, in the contract's namespace by default; `<index>~name` for another) and `tags[]` (`name`, `mode`, `min`/`max`/`step`/`period`/`tau`/`ratedRpm`) |

Durations are ISO 8601 (`PT30S`).  `TagSpec` refuses `max<=min` where a range is used, non-positive `period`/`tau`,
and a non-positive `step` for `randomWalk`/`counter`, at startup, naming the tag.

## Status codes

Every reading carries a StatusCode - its data quality,
[OPC 10000-4 §7.38](https://reference.opcfoundation.org/specs/OPC-10000-4/7.38).  **The pubsub transport delivers it**:
the PLC server's node holds the whole DataValue, the writer publishes it DataValue-encoded (`PubSub::Writer`'s
`STATUSCODE` field content mask), and the OpcServer's reader writes value and status into the target variable.  A
session write carries it too, but a server only takes a non-Good status from a session with `StatusWrite` on the node's
AccessLevel *and* on the user's
([OPC 10000-3 §8.57](https://reference.opcfoundation.org/specs/OPC-10000-3/v1.05.06/8.57)); the pumps nodeset is
AccessLevel 3 and `-grant` stops at `Read|Update|Subscribe`, so the OpcServer answers `BadWriteNotSupported`, the
emulator WARNs once per tag per session and writes the value alone from there - under `-transport=write` the quality
shows in the emulator's status line and nowhere downstream.  Under `-transport=server` the PLC's own node holds the
whole DataValue, as under pubsub, and the gateway collects it with the status.

A tag is Good unless its config says otherwise, and the stock config says nothing.

[`Opc.PlcEmulator.Quality.jsonnet`](config/Opc.PlcEmulator.Quality.jsonnet) is the overlay that schedules faults.  It
shows one example of every shape the UI decodes, on a 2 min cycle with one scheduled fault at a time:

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
beside the value (error, warning or info).  The tooltip gives the name, its flags and the numeric code.

The **Status** column spells it out and takes the icon over.  It is hidden in the default view; switch it on in the
view editor.

A Bad row keeps the last value it had, dimmed and locked, until a reading that is not Bad arrives.  See
`Variable.setReading` in [`node.ts`](../../../web/opc/control/src/lib/model/node.ts), and the decoding in
[`status-code.ts`](../../../web/opc/control/src/lib/model/status-code.ts).

## Trust

Two certificates of its own, and two lists of others':

| certificate | where | trusted by |
|---|---|---|
| login (`http.ssl`, CN `PlcEmulator.debug.webServer`) | `Jde-Cpp/PlcEmulator/ssl/certs/…webServer.PlcEmulator.pem` | the AppServer: its `/access/trustedCertDirs` lists the `PlcEmulator` dir (the dev configs; an installed hub's `config/args/install` lists only the OpcServer's - add this one there); the CN becomes the user |
| UA pair (`emulator.ssl`, CN `PlcEmulator.opc`, SAN `urn:jde:plc-emulator`) - the client's channel certificate on the OpcServer and, unless `plc.ssl` says otherwise, the PLC server's own | same dir | the OpcServer: the same `trustedCertDirs` entry (again the dev args; the installed ones need it added); the gateway: `gateway.trustedCertDirs` in the dev `Opc.Gateway.jsonnet`, for `-transport=server`.  Both rescan on a failed verify, so a re-issued cert is picked up |
| the OpcServer's own | `Jde-Cpp/OpcServer/ssl/certs` | this process: `/emulator/trustedCertDirs` in this config, checked before every session (`verifyServerCertificate`) - the anchors, not the OS root store |
| the gateway's per-connection certificates | `Jde-Cpp/OpcGateway/ssl/certs`, `Jde-Cpp/OpcHub/ssl/certs` | the PLC server: `plc.trustedCertDirs`, on the secured channel and as certificate tokens; a refused one is written to `plc.rejectedCertDir` |

The login TLS handshake with the AppServer is anchored separately (`web.client.ssl.caFile` = the AppServer's own cert;
the hub overlay swaps in the hub's).

## Security

The Part 14 path is a **plaintext, unauthenticated write** into the OpcServer's address space: a `DataSetReader` lands
received fields through the server-internal write - no session, no `OpcAuthorize` - and its only filter is the
`publisherId`/`writerGroupId`/`dataSetWriterId` triple in a tracked config file.  Anyone who can reach the url drives
`pump*.motorRpm`, and the default url is a multicast group (`opc.udp://224.0.0.22:4840/`), i.e. the multicast domain.
This open62541 build has no SKS.  That is why the reader lives in the `Opc.Server.Emulator*` overlays and not in
the stock OpcServer config, and why `PubSub::Reader` WARNs at startup when it is present: a demo affordance, not a
production one.  `-transport=write` has none of this - it is an authenticated session under the acl `-grant` wrote.

The PLC's own server is the UA server library's endpoint: the secured policies with the None one beside them
(`plc.none`), a trust list of client certificates, and a login through the hub - an issued token the hub vouches for,
a certificate that is a hub user's key, or a `plc.users` name.  Every node is then open to the session: the login is
the gate.  Anonymous is off unless `plc.tokenTypes.anonymous` turns it on, which opens the pump tags to anyone who can
reach the port.

## Network

Multicast needs a route (a box with a default route has one; a bare container may not).  The unicast form
`opc.udp://127.0.0.1:<port>/` works everywhere - set it in the contract, which both ends import, or in an overlay on
each side; the test configs use it.

## Tests

`Jde.Opc.PlcEmulator.Tests` (`tests/`): `Signals` (parsing, every refusal, each generator's shape), `Quality` (the
status names, the §7.38 bit layout, the windows, hold and the sensor range), `ParseDevices`, and `PlcServer` on loopback
4851 with the writer aimed at a port nothing reads: the `/emulator/plc` block and its defaults, a secured session with
a `plc.users` login reading and writing its nodes, anonymous, an issued token (there is no hub in the suite) and a
stranger's certificate refused, and a command write reaching the emulator's queue.  `ctest -R Jde.Opc.PlcEmulator.Tests`.
The OpcServer suite's `PubSubTests` covers the reader end with an in-process publisher of the same shape - values, and
`PublishedStatusLandsInTargetVariable` for a status crossing the wire.  The end to end of `-transport=server`, a hub
group collecting from the PLC, is checked by hand (*Server transport*) until a gateway test starts the emulator
in-process.

## Known limits

- `UA_Client_connect` is synchronous: a failed attempt stalls the device clock for up to the 10 s client timeout.
- A field or tag is written as a `Boolean`, an integer, an enumeration, a `Float` or a `Double`; the pumps contract is
  all `Double`.  Over the session (`-transport=write`) every number goes as a `Double`.
- Status codes reach the OpcServer over pubsub only - a session-written tag's non-Good status is refused (no
  `StatusWrite`) and its value is written alone.  A Bad reading always carries its last value: the OpcServer's reader
  skips a field without one.  The source timestamp is not held with a held reading, and the historian bits are not
  emulated.
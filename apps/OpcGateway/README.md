# Jde.Opc.Gateway

`Jde.Opc.Gateway` holds the OPC UA client sessions on the servers the website connects to, and
it is the website's way in to them.

The OPC UA client is [`libs/opc`](../../libs/opc), over [open62541](https://www.open62541.org/).

This app puts GraphQL and a websocket over it, keeps the server connections, and turns a web session into an OPC
session.

The installers ship [`Jde.Opc.Hub`](../OpcHub/README.md) which links this and the AppServer into one process.

The standalone `Jde.Opc.Gateway` still builds, for split deployments with N gateways per AppServer.  It is not
installed.

| | value |
|---|---|
| exe / lib / tests | `Jde.Opc.Gateway` / `Jde.Opc.GatewayLib` / `Jde.Opc.Tests` |
| settings / log | `config/Opc.Gateway.jsonnet` / `Opc.Gateway.log` |
| port | 1968 (`/http`): REST, `/graphql` and the OPC websocket; the hub serves the same on 1967, the socket at `/opc` |

## What it does

- **Server connections** (`config/opcGateway-meta.jsonnet`, the `server_connections` table): the OPC servers the site
  can reach, each a url, a certificate uri and a slug.  `config/release-opcServer.mutation` seeds the bundled
  `Jde.Opc.Server` as the default connection, which the installers apply with the OPC UA Server component.
  `config/access-opcGateway.mutation`, the `OpcGateway` group and role, is applied by nothing yet.

  A row's `kind` says what it connects to: `ua`, the default, or `open-protocol`, a tightening controller at
  `open-protocol://host[:port]` with a time zone and revision ceilings in `driver`
  ([`spec.md`](../../docs/open-protocol/spec.md), *The row*).  The gateway's clients dial such a row's own UA server on loopback, at `/gateway/drivers/portBase` plus the
  row's id, in place of its url.
- **Driver servers** (`src/drivers/`): a row of a kind with a driver gets a UA server of the gateway's own
  (`DriverHost`), started with the gateway and with the row, restarted when its url, kind or `driver` is edited and
  stopped when it is deleted.  It presents the process's `/http/ssl` certificate, takes Sign & Encrypt alone, and admits
  the gateway's own clients and no other: a web session by its issued token, a group's collector as the gateway's own
  user.  Every node is readable by any session and none is writable.  Behind it sits the kind's `IDriver`.  The Open
  Protocol one (`src/drivers/openProtocol/`) keeps a session with the controller under the
  `/gateway/drivers/openProtocol` settings and serves three objects.  `Result` has a variable per field of the
  controller's last tightening result, MID 0061 at revisions 1 to 7: each result's fields carry one source timestamp,
  the controller's time read in the row's zone and kept distinct and increasing, and a field the controller's revision
  lacks reads `Bad_NotSupported`.  `Counters` are `Total`, `Ok` and `Nok`, the tightenings since the driver started.
  `_Op` is the link's diagnostics: `Connected`, `Since`, `Url`, `LastError`, `Reconnects`, `Identity`, `Revisions`,
  `LinkLevel`, `Results`, `LostResults` and `Skew`, the controller's clock against the gateway's.  A group's member
  over a `Result` variable records every result when it has no `MinTimeInterval`, and the last of each interval when
  it has one.  A link that returns is filled: the results the controller made while it was down are fetched by MID
  0064, `maxBackfill` requests at most, and written in order ahead of the ones pushed since, each with what MID 0065
  carries of it, so its limits read `Bad_NotSupported` until the next pushed result; a walk that ends short of the
  last result written adds one to `LostResults` and warns.  Nothing is fetched before a row's first result of a run.
- **OPC sessions** (`src/UAClient.*`, `src/auth/`): one client per connection and credential.  Each is pinged every
  `/gateway/pingInterval` and dropped after `/gateway/ttl` idle.  A web session reaches a server in one of three ways:
  - As an issued token.  The token is the hub session id, which a Jde OpcServer resolves to the web user.
  - As a username and password on that server.  `POST /login` takes a JSON body `{opc, user, password}`, and the
    session is minted through the AppServer's `AddSession`.  The site's login page sends it when the user types
    `<slug>\<user>`.
  - As the gateway itself, by certificate.

  The secure channel uses a certificate the gateway issues per connection (`/gateway/issuedCerts`).  Its SAN is the
  gateway's applicationUri.  Every server's certificate is checked against `/gateway/trustedCertDirs`, unless
  `/gateway/verifyServerCertificate` is off.
- **The historian** (`src/hist/`, `/gateway/hist`, [`libs/historian/docs/spec.md`](../../libs/historian/docs/spec.md)):
  groups of nodes the gateway collects and stores under `/gateway/hist/path`.  Groups and templates are the
  `histGroups`, `histGroupNodes`, `histTemplates` and `histTemplateNodes` tables.  No `hist` block, no historian.
- **The OPC UA endpoint** (`src/server/`, `/gateway/server`, [`docs/servers/spec.md`](../../docs/servers/spec.md)): the
  gateway as a server, on 4841, over the shared UA server library (`libs/opc/server`).  No block, no endpoint.

  Its sessions are hub sessions, by certificate, issued token or user name.

  Its address space is a folder per connection, each with a `_Connection` object (`Connected`, `Since`, `Url`,
  `LastError`, `Reconnects`).  Under it are every historian group's members at their paths on the source, in a
  namespace per connection and remote URI.

  Each member serves the last value the collector delivered, with the source's timestamps, and `Bad_NoCommunication`
  through a connection's loss.  It also serves the group's history (`src/server/GatewayHistoryHost.*`) and writes
  through to the source (`src/server/ServedWrites.*`).

  `endpoint{ url policies trustedCertDirs rejectedCertDir served sessions }` describes it for the web's Endpoint tab
  and a group's page ([`src/ql/OpcSessionsQLAwait.h`](src/ql/OpcSessionsQLAwait.h)), null without one.
- **GraphQL** (`src/ql/`):
  - `serverConnections`, `opcSessions`, and `node` and `nodes` for browse and read.
  - `updateVariable` for a write, and `testConnection` to try a connection's settings before they are saved.
  - `call( opc, object, method, args )` for a method call, with `method( opc, id )` for its arguments.  They are read
    from the InputArguments/OutputArguments properties and cached per connection (`src/MethodArguments.*`).  An
    argument of a type of the server's own goes as the namespace-0 type it derives from (`src/BaseTypeCache.*`).
  - `refreshConditions( opc, node )` for ConditionRefresh with the caller's own subscription
    (`src/ql/RefreshConditionsQLAwait.*`).
  - `search` over a per-connection name index crawled on the first search (`src/NodeIndex.*`).
  - `__type(opc, ns, i)` for a server's enumerations, read from its DataType nodes (`src/EnumTypeCache.*`).
  - `history`, a server's own or a group's with `group`, and the `createHistory`/`updateHistory`/`upsertHistory`/
    `purgeHistory` edits.
  - `historyGroups` and `resolveHistory` for the historian.

  `config/introspection/*.jsonnet` extends the schema the site sees.
- **The OPC websocket** (`src/GatewaySocketSession.*`, protobuf, `src/types/proto`): subscribe and unsubscribe by node,
  the data changes pushed back, and GraphQL over the socket too.  The REST side is `POST /login`, `POST /logout` and
  `GET /ErrorCodes?scs=…`, which names UA status codes.

## Config

`config/Opc.Gateway.jsonnet` is the base config.  It targets a split `Jde.App.Server` on 1967, and anchors that
server's certificate for its login (`/web/client/ssl/caFile`).

There is no hub overlay, because the hub runs this role itself.

`-include` picks `config/args/<dialect>`: `sqlite`, `mysql` or `sqlServer`.  There is no default.  The config's
`import 'args.libsonnet'` resolves through it.

## Run

The executable is `<buildDir>/apps/OpcGateway/exe/Jde.Opc.Gateway`.  Run it from `<buildDir>/runtime`, since `-tests`
puts the log under the working directory's `logs/`, beside a split AppServer from the same tree, never a hub:

```bash
REPO_SOURCE_DIR=$JDE_DIR REPO_BUILD_DIR=$(dirname <buildDir>) ../apps/OpcGateway/exe/Jde.Opc.Gateway -c -tests \
  -settings=$JDE_DIR/apps/OpcGateway/config/Opc.Gateway.jsonnet -include=args/sqlite -arg path=<file>
```

`REPO_SOURCE_DIR` and `REPO_BUILD_DIR`, the build dir's parent, fill the paths `args/sqlite` reads.  ctest sets them
itself.

`-c` is console output.  `-tests` binds the ext vars the dev configs read; it is not a test mode.

## Tests

`Jde.Opc.Tests` ([`tests/`](tests)) embeds an AppServer on 2067, an OpcServer on 2070 / opc.tcp 4940 and the gateway on
2068, its endpoint on 4941, in one process.  These are the live ports +100, so it runs beside a live hub or service.
The embedded server loads the DI and IA nodesets from `$UA_NODE_SETS`, which must be set.

Under ctest it runs on in-memory sqlite: `ctest -R Jde.Opc.Tests`.  ctest stops it after `JDE_TEST_TIMEOUT` seconds
(450 by default).  `ctest --timeout` does not change that; raise it at configure with `-DJDE_TEST_TIMEOUT=<seconds>`.

- `SubscribeTests`, `BrowseTests`, `ReadRequestTests`, `WriteTests`: the OPC operations end to end.
- `QLTests`: the GraphQL surface, the introspection extensions included.
- `HistTests`, `HistEditTests`: a server's own history, read and edited through the gateway's QL.
- The gateway's historian:
  - `CredentialTests`, `CollectorTests`: the collector's own credential, and the client of its own it gets.
  - `HistGroupTests`, `HistPathTests`: groups and templates.  They cover the tables, each edit's live side,
    `resolveHistory` and a node's path.
  - `HistCollectorTests`: a group collecting, read and edited with `group`, beside a pass-through read of the same
    range.
  - `HistSubscribeTests`: historian subscriptions over the socket.  Snapshot pages come first, then the records as
    stored.
- The gateway's OPC UA endpoint:
  - `GatewayServerTests`: logins by issued token, certificate and user name, and the `_Connection` folders.
  - `GatewayNodesTests`: a group's members served at their paths, with their attributes and rights.
  - `GatewayValuesTests`: live values, checked against the group's last record.
  - `GatewayHistoryTests`: raw and processed HistoryRead, checked against the QL `history` with `group`, and
    HistoryUpdate through the endpoint.
  - `GatewayWritesTests`: a client's Write, held for the source's answer.
- `AppClientTests`, `ConnectCoalesceTests`, `RemoveClientTests`, `KeepAliveTests`: the AppServer link and the client
  life cycle.
- `DriverRowTests`: a connection of a kind with a driver ([plan](../../docs/open-protocol/plan.md) *Phase 3*) - the
  row's `kind` and `driver`, its save rules, the dial of the driver's server at 4980 plus the row's id, and
  `testConnection` against the Open Protocol library's test controller on 4645.
- `DriverHostTests`: a driver row's server and the Open Protocol driver's `_Op` (the plan's *Phase 4*), against the
  same controller - a row made, deleted, restored and edited while the gateway runs, the link lost and back, the
  controller's clock, a group collecting as the gateway's own user, a web session after its certificate is issued
  again, the server's certificate refused by a client with no anchor, a held port, and a controller the driver
  refuses.
- `DriverResultTests`: the Open Protocol driver's results (the plan's *Phase 5*), against the same controller - a
  result of each revision of MID 0061 in `Result`, two in a second kept distinct, the counters, a group recording a
  burst whole and the last result of each `MinTimeInterval`, and the endpoint's processed read of a served result
  against the QL's `history`.
- `DriverBackfillTests`: the results a lost link kept back (the plan's *Phase 6*), against the same controller - an
  outage's results of two stations filled in order ahead of the one held, what a MID 0065 gives `Result`, a result
  that came both ways written once, a walk that runs out of `maxBackfill`, a refused MID 0064, a link lost during the
  walk, ids that began again, a first connection that fetches nothing, and a stored result's hour that comes twice.
- `CalendarClientTests`: the cache of the AppServer's shift calendars
  ([spec](../../docs/calendar/spec.md) *The client*) - a calendar created, a shift updated and a delete each followed,
  a refused read leaving nothing held, and a reconnect of the app socket bringing the edit made while it was down.
- [`auth/`](tests/auth): credentials, certificates and server trust.  `TokenTests`, `PasswordTests`,
  `PlaintextPasswordTests`, `NoSecurityTests`, `SecurityPolicyTests`, `CertTests`, `CertFileTests`, `TrustReloadTests`,
  `ApplicationUriTests`, `ServerTrustTests`, `ServerTrustLiveTests`, `RefusedCertificateTests`,
  `RefusedCertificateDetailTests`, `ExternalServerTests`.
- `ServerCnnctnDBTests`, `HostNameTests`, `LogTests`, `UAClientExceptionTests`, `BrowseResponseTests`,
  `FromServerTests`: the rest.

## Soak

[`soak/`](soak) is `Jde.Opc.Soak` and `soak.sh`, the 24-hour soak harness.  It launches the split AppServer, OpcServer
and gateway plus the soak client against fresh sqlite files, watches liveness and memory, and writes a verdict.  It is
shaped around the three split exes, not the hub.

`soak.sh` is the entry point; it runs `Jde.Opc.Soak` three ways, each with `-settings=soak/config/Opc.Soak.jsonnet`:

- `-createCert`: issues the gateway's client certificate for each leg with a certificateUri, into the gateway's own
  cert tree, and prints the file an external server has to trust.
- `-grant`: grants the soak user AllAccess on the OpcServer's `nodeIds` resource.  `soak.sh` runs it after the
  OpcServer's first boot, then restarts the OpcServer to load it.
- No mode flag: the run itself.  It writes on every leg, checks that each push comes back, samples status and writes
  the summary the verdict reads.

`soak.sh --smoke` is a 10-minute validation run.  `--duration`, `--external*`, `--session-timeout` and the rest are
documented at the top of [`soak.sh`](soak/soak.sh), with the verdict criteria.  `UA_NODE_SETS` must be set, as for the
tests.
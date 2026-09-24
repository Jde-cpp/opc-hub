# Jde.Opc.Gateway

The gateway role: the OPC UA client sessions on the servers the site connects to, and the site's way in to them.  The
OPC UA is [`libs/opc`](../../libs/opc) over [open62541](https://www.open62541.org/); this app puts GraphQL and a
websocket over it, keeps the server connections, and turns a web session into an OPC session.

[`Jde.Opc.Hub`](../OpcHub/README.md) links this and the AppServer into one process, which is what the installers ship.
The standalone `Jde.Opc.Gateway` still builds for split deployments, N gateways per AppServer, and is not installed.

| | value |
|---|---|
| exe / lib / tests | `Jde.Opc.Gateway` / `Jde.Opc.GatewayLib` / `Jde.Opc.Tests` |
| `Process::AppName()` (its `connections{programName}` row on the AppServer, which `/opcGateways` lists) | `Jde.OpcGateway` |
| `Process::ProductName()` (`$(ProgramData)/Jde-Cpp/<product>`: its certificates and keys, the per-connection OPC certificates, `ssl/servers` for OPC servers' certificates) | `OpcGateway` |
| settings / log | `config/Opc.Gateway.jsonnet` / `Opc.Gateway.log` |
| port | 1968 (`/http`): REST, `/graphql` and the OPC websocket; the hub serves the same on 1967, the socket at `/opc` |

## What it does

- **Server connections** (`config/opcGateway-meta.jsonnet`, the `server_connections` table): the OPC servers the site
  can reach, each a url, a certificate uri and a slug.  `config/release-opcServer.mutation` seeds the bundled
  `Jde.Opc.Server` as the default connection, which the installers apply with the OPC UA Server component.
  `config/access-opcGateway.mutation`, the `OpcGateway` group and role, is applied by nothing yet.
- **OPC sessions** (`src/UAClient.*`, `src/auth/`): one client per connection and credential, pinged every
  `/gateway/pingInterval` and dropped after `/gateway/ttl` idle.  A web session reaches a server as the hub session id
  presented as an issued token, which a Jde OpcServer resolves to the web user; as a username and password on that
  server after `POST /login` with `<slug>\<user>`, which is also the site's password login, minted through the
  AppServer's `AddSession`; or as the gateway itself, by certificate.  The secure channel uses a certificate the gateway
  issues per connection (`/gateway/issuedCerts`, its SAN the gateway's applicationUri), and every server's certificate
  is checked against `/gateway/trustedCertDirs` unless `verifyServerCertificate` is off.
- **GraphQL** (`src/ql/`): `serverConnections`, `opcSessions`, `node` and `nodes` for browse and read, `updateVariable`
  for a write, `search` over a per-connection name index crawled on the first search (`src/NodeIndex.*`), and
  `__type(opc, ns, i)` for a server's enumerations, read from its DataType nodes (`src/EnumTypeCache.*`).
  `config/introspection/*.jsonnet` extends the schema the site sees.
- **The OPC websocket** (`src/GatewaySocketSession.*`, protobuf, `src/types/proto`): subscribe and unsubscribe by node,
  the data changes pushed back, and GraphQL over the socket too.  The REST side is `POST /login`, `POST /logout` and
  `GET /ErrorCodes?scs=…`, which names UA status codes.

`gatewayStartup.h` splits the start into `Configure` (the schema, the QL, the client certificate), the listener, and
`Connect` (the AppServer login and socket, the access snapshot), so the hub can put its own listener and QL between them.

## Config

`config/Opc.Gateway.jsonnet` is the base, against a split `Jde.App.Server` on 1967, whose certificate it anchors for its
login (`/web/client/ssl/caFile`).  There is no hub overlay: the hub runs this role itself.  `config/args/<dialect>`
(`sqlite`, `mysql`, `sqlServer`) is picked with `-include`; there is no default, the config's `import 'args.libsonnet'`
resolves through it.

## Run

The executable is `<buildDir>/apps/OpcGateway/exe/Jde.Opc.Gateway`.  Run it from `<buildDir>/runtime`, since `-tests`
puts the log under the working directory's `logs/`, beside a split AppServer from the same tree, never a hub:

```bash
../apps/OpcGateway/exe/Jde.Opc.Gateway -c -tests -settings=$JDE_DIR/apps/OpcGateway/config/Opc.Gateway.jsonnet \
  -include=args/sqlite -arg path=<file>
```

`-c` is console output.  `-tests` binds the ext vars the dev configs read; it is not a test mode.

## Tests

`Jde.Opc.Tests` ([`tests/`](tests)) embeds an AppServer on 1967, an OpcServer on 1970 / opc.tcp 4840 and the gateway on
1968 in one process, so it cannot run beside a live hub or AppServer.  The embedded server loads the DI and IA nodesets
from `$UA_NODE_SETS`, which must be set.  Under ctest it runs on in-memory sqlite:
`ctest --timeout 300 -R Jde.Opc.Tests`.

- `SubscribeTests`, `BrowseTests`, `ReadRequestTests`, `WriteTests`: the OPC operations end to end.
- `QLTests`: the GraphQL surface, the introspection extensions included.
- `AppClientTests`, `ConnectCoalesceTests`, `RemoveClientTests`: the AppServer link and the client life cycle.
- `ServerCnnctnDBTests`, `HostNameTests`, `LogTests`, `UAClientExceptionTests`, `BrowseResponseTests`: the rest.

[`soak/`](soak) is `Jde.Opc.Soak` and `soak.sh`, the 24-hour soak harness: it launches the split AppServer, OpcServer
and gateway plus the soak client against fresh sqlite files, watches liveness and memory, and writes a verdict.  It is
shaped around the three split exes, not the hub.

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
- **GraphQL** (`src/ql/`): `serverConnections`, `opcSessions`, `node` and `nodes` for browse and read, `updateVariable`
  for a write, `search` over a per-connection name index crawled on the first search (`src/NodeIndex.*`), and
  `__type(opc, ns, i)` for a server's enumerations, read from its DataType nodes (`src/EnumTypeCache.*`).
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
- `HistTests`, `HistEditTests`: a server's own history, read and edited through the gateway's QL.
- `AppClientTests`, `ConnectCoalesceTests`, `RemoveClientTests`, `KeepAliveTests`: the AppServer link and the client
  life cycle.
- [`auth/`](tests/auth): credentials, certificates and server trust.  `TokenTests`, `PasswordTests`,
  `PlaintextPasswordTests`, `NoSecurityTests`, `SecurityPolicyTests`, `CertTests`, `CertFileTests`, `TrustReloadTests`,
  `ApplicationUriTests`, `ServerTrustTests`, `ServerTrustLiveTests`, `RefusedCertificateTests`,
  `RefusedCertificateDetailTests`, `ExternalServerTests`.
- `ServerCnnctnDBTests`, `HostNameTests`, `LogTests`, `UAClientExceptionTests`, `BrowseResponseTests`,
  `FromServerTests`: the rest.

[`soak/`](soak) is `Jde.Opc.Soak` and `soak.sh`, the 24-hour soak harness: it launches the split AppServer, OpcServer
and gateway plus the soak client against fresh sqlite files, watches liveness and memory, and writes a verdict.  It is
shaped around the three split exes, not the hub.

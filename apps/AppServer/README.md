# Jde.App.Server

The application server. It runs:

- the registry the other processes log in to;
- the sessions and logins the site uses;
- the GraphQL over the access tables and its own;
- the log collector.

[`Jde.Opc.Hub`](../OpcHub/README.md) links this and the OpcGateway into one process. The installers ship the hub.

The standalone `Jde.App.Server` + `Jde.Opc.Gateway` pair still builds, for split deployments with N gateways per
AppServer. The OpcServer and the PLC emulator run against either.

The standalone does not serve the site. Only the hub's config names a site directory (`/http/site`).

| | value |
|---|---|
| exe / lib / tests | `Jde.App.Server` / `Jde.App.ServerLib` / `Jde.App.Server.Tests` |
| `Process::AppName()` (its own `connections{programName}` row) | `Jde.AppServer` |
| `Process::ProductName()` (`$(ProgramData)/Jde-Cpp/<product>`: its certificates and keys) | `AppServer` |
| settings / log | `config/App.Server.jsonnet` / `App.Server.log` |
| port | 1967 (`/http`): REST, `/graphql`, and the app-protocol websocket at `/` |

## What it does

- **REST** (`src/HttpRequestAwait.cpp`): `/login` and `/logout` (a bearer JWT in, a session out), `/GoogleAuthClientId`,
  `/opcGateways` and `/opcServers` (the registered instances the site picks a gateway from), and `/graphql` by GET or
  POST.  The routes are free functions so a host that serves two apps from one listener (the hub) can call them.
- **The app protocol** (`src/ServerSocketSession.*`, protobuf over a websocket, messages in `libs/app/shared/proto`).
  A process on the socket:
  - registers as an instance and gets its `connections` row;
  - streams its log entries in. They are archived (the site's Logs tab reads the archive) and pushed to whoever holds a
    log subscription;
  - asks for sessions and JWTs;
  - runs GraphQL.

  Requests also go the other way. The server queries an instance over its socket (`ClientQuery`) for the log-level
  push and for the delegated admin check an instance's acl asks for.

  `ForwardExecution`, a client's request to relay an execution to an instance, is in the protocol. Nothing sends it
  today.
- **Data** ([`config/app-meta.jsonnet`](config/app-meta.jsonnet)): `programs`, `instances`, `connections`, `hosts`,
  `logLevels`, `instanceTagLevels`, beside the access library's users, groups, roles and resources in the same
  database.  The per-dialect procedures are under [`config/sql`](config/sql); for sqlite they are C++
  (`config/sql/sqlite`, the `Jde.DB.Sqlite.AppServer` module), which also carries the access library's.
- **Trust** (`/access/trustedCertDirs`): a client certificate dropped in one of these dirs may enroll by key login. The
  dirs are the OpcServer's, a split gateway's and the PLC emulator's. Production products only; the test binaries
  anchor their own dirs.

`appStartup.h` splits the start into `Configure` and the listener. `Configure` sets up the db, schema sync, the access
snapshot, this process's connection row, the signing key and the QL hooks.

`ConfigureOptions` serves a host that adds schemas and its own QL over them. It is the seam the hub composes through.

## Run

The executable is `<buildDir>/apps/AppServer/exe/Jde.App.Server`. Run it from `<buildDir>/runtime`, since `-tests` puts
the log under the working directory's `logs/`. `reconfig` makes `runtime/logs`; after a raw configure, make it first.

First export the two roots the sqlite args expand. Only ctest sets them.

```bash
export REPO_SOURCE_DIR=$JDE_DIR REPO_BUILD_DIR=$(dirname <buildDir>)
../apps/AppServer/exe/Jde.App.Server -c -tests -settings=$JDE_DIR/apps/AppServer/config/App.Server.jsonnet \
  -include=args/sqlite -arg path=<file>
```

`-c` is console output. `-tests` binds the ext vars the dev configs read; it is not a test mode.

Never run it beside a hub. Both listen on 1967.

`-include` picks `config/args/<dialect>`:

- `sqlite`
- `mysql`
- `sqlServer`

There is no default. The config's `import 'args.libsonnet'` resolves through it.

## Tests

`Jde.App.Server.Tests` ([`tests/`](tests)) embeds the server on 1972, so it runs beside a live AppServer or hub. Its
suites:

- `HttpRoutingTests`: the routes;
- `SessionMapTests` and `ProcessTransmissionTests`: the socket protocol, including the failed-adoption close and the
  delegated admin check;
- `RegistrationTests`: concurrent registrations;
- `InstanceTagLevelTests`: the log-level push;
- `LogDataTests`: the connection rows.

Under ctest it runs on in-memory sqlite:

```bash
ctest -R Jde.App.Server.Tests
```

ctest kills it after 300 s. That limit is the `JDE_TEST_TIMEOUT` cache variable, which wins over `ctest --timeout`.
Raise it with `-DJDE_TEST_TIMEOUT=<seconds>`.

# Jde.App.Server

The application server: the registry the other processes log in to, the sessions and logins the site uses, the GraphQL
over the access tables and its own, and the log collector.

[`Jde.Opc.Hub`](../OpcHub/README.md) links this and the OpcGateway into one process, which is what the installers
ship.  The standalone `Jde.App.Server` + `Jde.Opc.Gateway` pair still builds for split deployments, N gateways per
AppServer, and the OpcServer and the PLC emulator run against either.  The standalone does not serve the site: only the
hub's config names a site directory (`/http/site`).

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
- **The app protocol** (`src/ServerSocketSession.*`, protobuf over a websocket, messages in `libs/app/shared/proto`):
  a process registers as an instance and gets its `connections` row; it streams its log entries in, which are archived
  (the site's Logs tab reads the archive) and pushed to whoever holds a log subscription; it asks for sessions and
  JWTs; and GraphQL runs over the socket too.  Requests also go the other way: the server queries an instance over its
  socket (`ClientQuery`) - the log-level push, and the delegated admin check an instance's acl asks for.
  `ForwardExecution`, a client's request to relay an execution to an instance, is in the protocol but nothing sends it
  today.
- **Data** ([`config/app-meta.jsonnet`](config/app-meta.jsonnet)): `programs`, `instances`, `connections`, `hosts`,
  `logLevels`, `instanceTagLevels`, beside the access library's users, groups, roles and resources in the same
  database.  The per-dialect procedures are under [`config/sql`](config/sql); for sqlite they are C++
  (`config/sql/sqlite`, the `Jde.DB.Sqlite.AppServer` module), which also carries the access library's.
- **Trust** (`/access/trustedCertDirs`): a client certificate dropped in one of these dirs may enroll by key login - the
  OpcServer, a split gateway, the PLC emulator.  Production products only; the test binaries anchor their own dirs.

`appStartup.h` splits the start into `Configure` (db, schema sync, the access snapshot, this process's connection row,
the signing key, the QL hooks) and the listener, with `ConfigureOptions` for a host that adds schemas and its own QL over
them - the seam the hub composes through.

## Run

The executable is `<buildDir>/apps/AppServer/exe/Jde.App.Server`; run it from `<buildDir>/runtime`, since `-tests` puts
the log under the working directory's `logs/`:

```bash
../apps/AppServer/exe/Jde.App.Server -c -tests -settings=$JDE_DIR/apps/AppServer/config/App.Server.jsonnet \
  -include=args/sqlite -arg path=<file>
```

`-c` is console output; `-tests` binds the ext vars the dev configs read and is not a test mode.  Never beside a hub -
they share 1967.  `config/args/<dialect>` (`sqlite`, `mysql`, `sqlServer`) is picked with `-include`; there is no
default, the config's `import 'args.libsonnet'` resolves through it.

## Tests

`Jde.App.Server.Tests` ([`tests/`](tests)) embeds the server on 1972, so it runs beside a live AppServer or hub.
`HttpRoutingTests` covers the routes, `SessionMapTests` and `ProcessTransmissionTests` the socket protocol (including
the failed-adoption close and the delegated admin check), `RegistrationTests` concurrent registrations,
`InstanceTagLevelTests` the log-level push, `LogDataTests` the connection rows.  Under ctest it runs on in-memory
sqlite: `ctest --timeout 300 -R Jde.App.Server.Tests`.

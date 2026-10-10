# Jde.Opc.Server

Our OPC UA server, on [open62541](https://www.open62541.org/).

The address space is loaded from NodeSet2 files: the OPC Foundation's DI and IA companion specs, plus a pumps demo of
our own.

The session login and the node-level access control come from the hub.

The installers ship it as the **OPC UA Server** component, and with that component it is the hub's first connection.
The hub connects to any other OPC UA server the same way, so nothing here is required.

Under [`emulator/`](emulator/README.md) is `Jde.Opc.PlcEmulator`, a stand-in for a UA-enabled PLC that keeps the pumps'
values moving.  Without it every value in the shipped address space is static.

| | value |
|---|---|
| exe / lib / tests | `Jde.Opc.Server` / `Jde.Opc.ServerLib` / `Jde.Opc.Server.Tests` |
| settings / log | `config/Opc.Server.jsonnet` / `Opc.Server.log` |
| opc.tcp | 4840 (`/opcServer/port`) |
| https | 1970 (`/http`): `/graphql` only, for the log settings and the `logs` query the web UI's Logs tab reads.  It is the entry `/opcServers` lists, and its certificate is the one the hub login uses |

## What it does

- **Address space**: the NodeSet2 files at `/opcServer/configFiles`, in order: the DI nodeset, the IA nodeset and its
  examples, then [`config/nodesets/pumps.NodeSet2.xml`](config/nodesets/pumps.NodeSet2.xml), the emulator's tags
  (`urn:jde:pumps`).  The first three come from `$(UA_NODE_SETS)`, a clone of
  [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset).  Nothing of the address space is in the
  database: [`config/opcServer-meta.jsonnet`](config/opcServer-meta.jsonnet) declares only the `nodeIds` resource the
  node acls hang off.
- **Login to the hub**: at startup it logs in to the hub's registry on 1967 with its `/http/ssl` certificate.
  - The login is a JWT signed with that key.  The hub admits it through its trust anchors, and the user is the one
    named by the certificate's CN.
  - If the hub is not up yet, it waits.
  - Each side anchors the other.  The hub anchors the server's `ssl/certs` dir, and the server anchors the hub's
    through `/web/client/ssl/caFile`.
  - The access snapshot comes over that connection and re-syncs on change.  It holds the users, groups, roles and
    node acls.  Log levels are pushed the same way.
  - `src/access/OpcAuthorize.*` is the `Access::Authorize` that translates our rights into open62541's access-level
    bits.
- **Session login** (`src/access/UAAccess.*`), three token types:
  - a certificate under `/access/trustedCertDirs`, matched to a hub user by its public key; `src/UATrust.*` rescans the
    dirs without a restart;
  - an issued token carrying a hub session, its id or a hub JWT - how the gateway role opens sessions for its web users;
    such a session is renewed against the hub ahead of its lapse;

  Anonymous is off by default (`/opc/tokenTypes`).  A session's rights are the hub user's.
- **History** (`src/hist/`): the server keeps the history of every variable a nodeset marks `Historizing="true"`.  It
  serves HistoryRead's raw, modified, at-time and processed reads, and HistoryUpdate's edits
  ([`libs/historian/docs/spec.md`](../../libs/historian/docs/spec.md), *OpcServer*).
  - Each such variable is configured by its `HA Configuration` object.  Its keys are `ExceptionDeviation` and its
    format, `MinTimeInterval`, `MaxTimeInterval` and `Stepped`.
  - The server adds what the nodeset leaves out, the object itself included.  Where the nodeset set no HistoryRead, it
    adds the HistoryRead and HistoryWrite bits to the node's `AccessLevel`.
  - The files go under `/opcServer/hist/path`, which the process locks.  With no `/opcServer/hist` block there is no
    history.
  - Reading a node's history takes Read on the node.  Editing it takes Update or Delete on the node, granted on an
    enforced resource.
  - The aggregates served are the ones the `AggregateFunctions` folder lists.  `Median` (`ns=1;s=Median`) is ours; the
    rest are Part 13's.
  - The pumps nodeset marks six variables.  They cover all four deviation formats, plus one variable with no HA
    Configuration and one with no format.
  - At shutdown the log says how long the history callbacks held open62541's service lock.  A read that holds it
    100 ms or more is warned of as it happens.
- **Part 14 PubSub** (`src/pubsub/PubSubReader.*`): a UADP `DataSetReader` for the contract in
  [`config/pubsub/pumps.libsonnet`](config/pubsub/pumps.libsonnet) that the emulator publishes with.  It is
  deliberately not in the stock config, because a reader writes its target nodes with no session and no acl.  So it
  is an overlay, below.

## Config

| file | use |
|---|---|
| [`config/Opc.Server.jsonnet`](config/Opc.Server.jsonnet) | the base: against a split `Jde.App.Server` on 1967 (its cert as the login TLS anchor) |
| [`config/Opc.Server.Hub.jsonnet`](config/Opc.Server.Hub.jsonnet) | against a `Jde.Opc.Hub` instead: the hub's cert as the anchor, nothing else changes |
| [`config/Opc.Server.Install.jsonnet`](config/Opc.Server.Install.jsonnet) | what the installed service loads: nodesets from the product dir, one sqlite file, the hub's cert |
| [`config/Opc.Server.Emulator.jsonnet`](config/Opc.Server.Emulator.jsonnet), [`config/Opc.Server.Emulator.Hub.jsonnet`](config/Opc.Server.Emulator.Hub.jsonnet) | the base and the hub overlay, each plus the PubSub reader, for the emulator's default `-transport=pubsub` |

`config/args/<dialect>` is the database and trust-dir half, picked with `-include`.  There is no default: the config's
`import 'args.libsonnet'` resolves through it.

- `sqlite`, `mysql` and `sqlServer` are the dev dialects.
- `install` is what the service loads.
- `install-user` is the Windows current-user install, which binds loopback only.

`config/nodesets/kitchen.xml` is a test fixture.

## Run

The executable is `<buildDir>/apps/OpcServer/exe/Jde.Opc.Server`.  Run it from `<buildDir>/runtime`, since `-tests`
puts the log under the working directory's `logs/`, beside a hub from the same tree:

```bash
../apps/OpcServer/exe/Jde.Opc.Server -c -tests -settings=$JDE_DIR/apps/OpcServer/config/Opc.Server.Hub.jsonnet \
  -include=args/sqlite -arg path=<file>
```

`-c` is console output.  `-tests` binds the ext vars the dev configs read; it is not a test mode.

The installed command lines are in [`../OpcHub/setup/README.md`](../OpcHub/setup/README.md) (Windows) and
[`../OpcHub/setup/linux/README.md`](../OpcHub/setup/linux/README.md).

The setup README's *First login* says what the component seeds on the hub: the server as the default connection,
and the Google login.

## Tests

`Jde.Opc.Server.Tests` ([`tests/`](tests)) embeds an AppServer on 1967 and the server on 1970 / opc.tcp 4840, so it
cannot run beside a live hub or AppServer.  Under ctest it runs on in-memory sqlite:
`ctest --timeout 300 -R Jde.Opc.Server.Tests`.

- `AccessTests`: sessions and node acls.
- `ToAccessTests`: the mapping from hub rights to open62541's access-level bits, without a server.
- `HistoryTests`: the pumps nodeset's historized variables, written through the server, then read and edited by a UA
  client's HistoryRead and HistoryUpdate; history under `<cwd>/logs/hist/opc-server-tests`, cleared at the start.
- `HistoryRestartTests`: a restart keeps each node's history index and removes what the nodesets no longer historize.
- `CustomMutationTests`: `updateLogSetting` is the only mutation the server answers, and only for a signed-in user.
  Anonymous gets 401, and every other mutation 403.
- `UALoadTests`: loads the companion-spec nodesets from `$UA_NODE_SETS`, which must be set.
- `PubSubTests`: a unicast reader on udp 4849.
- `TrustListTests`, `UAConfigTests`: the PKI setup.

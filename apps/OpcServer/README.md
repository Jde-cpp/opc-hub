# Jde.Opc.Server

Our OPC UA server, on [open62541](https://www.open62541.org/).  The address space is loaded from NodeSet2 files: the OPC
Foundation's DI and IA companion specs, plus a pumps demo of our own.  The session login and the node-level access
control come from the hub.

The installers ship it as the **OPC UA Server** component, and with that component it is the hub's first connection.
The hub connects to any other OPC UA server the same way, so nothing here is required.

Under [`emulator/`](emulator/README.md) is `Jde.Opc.PlcEmulator`, a stand-in for a UA-enabled PLC that keeps the pumps'
values moving.  Without it every value in the shipped address space is static.

| | value |
|---|---|
| exe / lib / tests | `Jde.Opc.Server` / `Jde.Opc.ServerLib` / `Jde.Opc.Server.Tests` |
| `Process::AppName()` (the Windows service name, and its `connections{programName}` row on the hub) | `Jde.OpcServer` |
| `Process::ProductName()` (`$(ProgramData)/Jde-Cpp/<product>`: certs, and on an install the sqlite file and the logs) | `OpcServer` |
| settings / log | `config/Opc.Server.jsonnet` / `Opc.Server.log` |
| opc.tcp | 4840 (`/opcServer/port`) |
| https | 1970 (`/http`): answers nothing yet; kept for the certificate the hub login uses, and it is the entry `/opcServers` lists |

## What it does

- **Address space**: the NodeSet2 files at `/opcServer/configFiles`, in order: the DI nodeset, the IA nodeset and its
  examples, then [`config/nodesets/pumps.NodeSet2.xml`](config/nodesets/pumps.NodeSet2.xml), the emulator's tags
  (`urn:jde:pumps`).  The first three come from `$(UA_NODE_SETS)`, a clone of
  [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset).  Nothing of the address space is in the
  database: [`config/opcServer-meta.jsonnet`](config/opcServer-meta.jsonnet) declares only the `nodeIds` resource the
  node acls hang off.
- **Login to the hub**: at startup it logs in to the hub's registry on 1967 with its `/http/ssl` certificate: a JWT
  signed with that key, admitted through the hub's trust anchors, the user named by the certificate's CN.  If the hub is
  not up yet it waits.  Each side anchors the other: the hub the server's `ssl/certs` dir, the server the hub's, through
  `/web/client/ssl/caFile`.  The access snapshot - users, groups, roles, the node acls - comes over that connection and
  re-syncs on change, and log levels are pushed the same way.  `src/access/OpcAuthorize.*` is the `Access::Authorize`
  that translates our rights into open62541's access-level bits.
- **Session login** (`src/access/UAAccess.*`), three token types:
  - a certificate under `/access/trustedCertDirs`, matched to a hub user by its public key; `src/UATrust.*` rescans the
    dirs without a restart;
  - an issued token carrying a hub session, its id or a hub JWT - how the gateway role opens sessions for its web users;
    such a session is renewed against the hub ahead of its lapse;
  - username/password against `/opc/users`, an opt-in list nothing shipped sets.

  Anonymous is off by default (`/opc/tokenTypes`).  A session's rights are the hub user's.
- **Part 14 PubSub** (`src/pubsub/PubSubReader.*`): a UADP `DataSetReader` for the contract in
  [`config/pubsub/pumps.libsonnet`](config/pubsub/pumps.libsonnet) that the emulator publishes with.  Deliberately not
  in the stock config - a reader writes its target nodes with no session and no acl - so it is an overlay, below.

## Config

| file | use |
|---|---|
| [`config/Opc.Server.jsonnet`](config/Opc.Server.jsonnet) | the base: against a split `Jde.App.Server` on 1967 (its cert as the login TLS anchor) |
| [`config/Opc.Server.Hub.jsonnet`](config/Opc.Server.Hub.jsonnet) | against a `Jde.Opc.Hub` instead: the hub's cert as the anchor, nothing else changes |
| [`config/Opc.Server.Install.jsonnet`](config/Opc.Server.Install.jsonnet) | what the installed service loads: nodesets from the product dir, one sqlite file, the hub's cert |
| [`config/Opc.Server.Emulator.jsonnet`](config/Opc.Server.Emulator.jsonnet), [`config/Opc.Server.Emulator.Hub.jsonnet`](config/Opc.Server.Emulator.Hub.jsonnet) | the base and the hub overlay, each plus the PubSub reader, for the emulator's default `-transport=pubsub` |

`config/args/<dialect>` is the database and trust-dir half, picked with `-include` - there is no default, the config's
`import 'args.libsonnet'` resolves through it.  `sqlite`, `mysql` and `sqlServer` are the dev dialects; `install` is what
the service loads, and `install-user` is the Windows current-user install, which binds loopback only.
`config/nodesets/kitchen.xml` is a test fixture.

## Run

The executable is `<buildDir>/apps/OpcServer/exe/Jde.Opc.Server`.  Run it from `<buildDir>/runtime`, since `-tests`
puts the log under the working directory's `logs/`, beside a hub from the same tree:

```bash
../apps/OpcServer/exe/Jde.Opc.Server -c -tests -settings=$JDE_DIR/apps/OpcServer/config/Opc.Server.Hub.jsonnet \
  -include=args/sqlite -arg path=<file>
```

`-c` is console output.  `-tests` binds the ext vars the dev configs read; it is not a test mode.  The installed command
lines are in [`../OpcHub/setup/README.md`](../OpcHub/setup/README.md) (Windows) and
[`../OpcHub/setup/linux/README.md`](../OpcHub/setup/linux/README.md).  What the component seeds on the hub, the server as
the default connection and the Google login, is the setup README's *First login*.

## Tests

`Jde.Opc.Server.Tests` ([`tests/`](tests)) embeds an AppServer on 1967 and the server on 1970 / opc.tcp 4840, so it
cannot run beside a live hub or AppServer.  Under ctest it runs on in-memory sqlite:
`ctest --timeout 300 -R Jde.Opc.Server.Tests`.

- `AccessTests`: sessions and node acls.
- `CustomMutationTests`: log settings is the only mutation the server answers over the socket, and only signed in.
- `UALoadTests`: loads the companion-spec nodesets from `$UA_NODE_SETS`, which must be set.
- `PubSubTests`: a unicast reader on udp 4849.
- `TrustListTests`, `UAConfigTests`: the PKI setup.

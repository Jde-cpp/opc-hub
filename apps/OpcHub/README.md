# Jde.Opc.Hub

The AppServer and the OpcGateway in one process.  `Jde.App.ServerLib` and `Jde.Opc.GatewayLib` link into one exe, and
`src/hubStartup.cpp` composes the two startups.

The gateway's app client is answered in-process (`src/HubAppClient.h`).  A split gateway logs in over loopback and a
websocket instead.

The standalone `Jde.App.Server` and `Jde.Opc.Gateway` still build, for split deployments of N gateways per AppServer.

| | value |
|---|---|
| exe / lib / tests | `Jde.Opc.Hub` / `Jde.Opc.HubLib` / `Jde.Opc.Hub.Tests` |
| `Process::AppName()` (service name, `connections{programName}`) | `Jde.OpcHub` |
| `Process::ProductName()` (`$(ProgramData)/Jde-Cpp/<product>`: certs, issued OPC certs, app data) | `OpcHub` |
| settings / log | `config/Opc.Hub.jsonnet` / `Opc.Hub.log` (derived by `Settings::FileStem()`) |
| port | 1967 (`/http`), shared by both roles |

The one port serves:

- `/` - the AppServer's REST and app-protocol socket.
- `/opc` - the gateway's REST and OPC socket.
- `/graphql` - one endpoint over access, app and gateway.

## Run

Stop any split AppServer first.  Both listen on 1967.

Run the hub in the foreground from `<buildDir>/runtime`.  Export the two roots the args expand first; only ctest sets
them.

```bash
export REPO_SOURCE_DIR=$JDE_DIR REPO_BUILD_DIR=$(dirname <buildDir>)
../apps/OpcHub/exe/Jde.Opc.Hub -c -tests -settings=$JDE_DIR/apps/OpcHub/config/Opc.Hub.jsonnet -include=args/mysql
```

For sqlite, use `-include=args/sqlite -arg path=<file>`.

From a second shell, check both apps' routes on the one port:

```bash
curl -s localhost:1967/opcGateways
curl -s localhost:1967/ErrorCodes?scs=2150891520 && curl -s localhost:1967/GoogleAuthClientId
```

For the OPC path, also start the OpcServer with `apps/OpcServer/config/Opc.Server.Hub.jsonnet`.  It anchors the hub's
cert for its login.

## Config

`config/Opc.Hub.jsonnet` imports both production configs and picks every top-level key explicitly (jsonnet `+`
replaces whole sub-objects).  `config/args/<dialect>/args.libsonnet` mounts `access` + `app` + `gateway` in one
catalog with the split apps' table prefixes, so the hub runs against the data a split AppServer + gateway created.
The hub has no meta or sql of its own.  Its mounts point at `libs/access/config`, `apps/AppServer/config` and
`apps/OpcGateway/config`.

Certs: `ProductName` puts the hub's tree under `$(ProgramData)/Jde-Cpp/OpcHub`.  The OpcServer and the PLC emulator
anchor the split AppServer's cert for their login TLS, so against a hub they need the overlays
`apps/OpcServer/config/Opc.Server.Hub.jsonnet` / `apps/OpcServer/emulator/config/Opc.PlcEmulator.Hub.jsonnet`
(`caFile` = the hub's `OpcHub.pem`); the OpcServer's args already trust `certsDir("OpcHub")` for the gateway role's
OPC client certs.  The installed args (`config/args/install`, the OpcServer's too) trust only what the installer ships -
the hub the OpcServer's dir, the OpcServer the hub's - so the emulator, or a split gateway, run against an installed
product is its dir added to that args file (the comment there) and the service restarted; a listed dir that is never
created is a warning in the log.

## Tests

`Jde.Opc.Hub.Tests` (`tests/`) embeds the hub (1973) plus an OpcServer (1975, opc.tcp 4842) - ports nothing else binds,
so it runs beside a live hub.  `ctest --timeout 300 -R Jde.Opc.Hub.Tests`.

## Frontend

Unchanged endpoint (`applicationServer` 1967).  The SPA discovers the gateway role through `/opcGateways` (same host:port)
and opens its OPC websocket on `/opc` (`Gateway.socketPath` in `web/opc/control/.../gateway-service.ts`); a standalone
gateway ignores the path, so one build serves both deployments.  The hub's one `connections{}` row (`Jde.OpcHub`) routes to
the gateway page (`app-resolver.ts`).

## Install

The installers ship the hub, the OpcServer and the Web UI.

The hub serves the Web UI itself, so a browser needs nothing but the hub's port.

What each installer lays down, the service command lines and what an uninstall leaves behind are in:

- [`setup/README.md`](setup/README.md) - Windows.  An NSIS installer, `OpcHubSetup-<version>.exe`, built by
  `setup/build-setup.ps1`.
- [`setup/linux/README.md`](setup/linux/README.md) - the `.deb` and the per-user tarball.

The settings they ship:

- `config/args/install` - sqlite.  `args/install-sqlServer` is the by-hand SQL Server variant.
- `apps/OpcServer/config/Opc.Server.Install.jsonnet`.

### Windows

1) Run `OpcHubSetup-<version>.exe`.
   - Install mode: **All users** registers the selected products as Windows services.  It needs administrator rights and
     installs the VC++ v14 x64 runtime when missing.
   - **Current user** installs under `%LOCALAPPDATA%\Programs` and runs the products from Start Menu shortcuts.  It
     needs no administrator rights.  When the runtime is missing it offers to install it, which asks for an
     administrator once.
   - Components:
     - the OPC Hub (`Jde.OpcHub`), required.  It is the AppServer and the OpcGateway in one process.
     - the OPC UA Server (`Jde.OpcServer`), optional.  It seeds the Web UI's login provider and the hub's default
       connection.
     - the Web UI.
     - Start at logon, current-user installs only, off by default.  It starts the selected products when you log on.
   - The finish page's "Start now" box starts the products; later, `net start Jde.OpcHub` / `net start Jde.OpcServer`, or
     the Start Menu shortcuts (a console window each).  The database is sqlite, one file per product under
     `C:\ProgramData\Jde-Cpp\<Product>`, created on the first start - no SQL Server, no setup script.
2) Browse to http://localhost:1967/ (the finish page's link) - from another machine, `http://<hub>:1967/`; the page calls
   the hub by the name it was browsed by.  The all-users installer allows inbound TCP 1967 (and 4840 with the OPC UA
   Server) through Windows Firewall; a current-user install cannot open a port, so it listens on loopback only (no firewall
   prompt) and the products answer that machine alone - for another machine, install for all users, or set
   `listenAddress: null` in its `args/install-user` and have an administrator open the ports.
3) Log in.  Which login you have depends on step 1's components - [`setup/README.md`](setup/README.md) "First login":
   - **With the OPC UA Server component**, it is **Google**, seeded ready to use along with the bundled server as the
     default connection.  The site's origin must be registered under the OAuth client id the hub serves, which the
     project's default covers for `http://localhost:1967` only - any other host needs its own client id.
   - **Without it**, the login is **an OPC server's own**, and adding the connection is what creates it: on
     [Applications](http://localhost:1967/apps) > the OpcHub card > *Connections* > *Add*, save the connection while still
     signed out, have the two sides trust each other's certificates, then sign in as **`<slug>\<user>`** -
     `plant1\operator1` - with that user's password on the server.  The Web UI's *Overview* help walks it as *First steps*.
     Walked against a KEPServerEX 6.12 on 2026-09-20: signed in 11 minutes from the start of the install, a value
     streaming at 13.
4) Uninstall: Settings > Apps (a current-user install also has an Uninstall shortcut in its Start Menu folder).  The
   database, certificates and logs under `C:\ProgramData\Jde-Cpp` are left in place.

IIS is optional - the site behind IIS instead of, or beside, the hub's own url.  Windows features: Internet Information
Services > World Wide Web Services > Common HTTP Features > *Static Content* and *Default Document*, nothing else.  IIS
Manager > Add Website: site name `OpcHub`, physical path `C:\Program Files\Jde-Cpp\Web` (the current-user install's
`Web` dir), binding http, port 8071.  Reloading or bookmarking a route (`/login`, `/gateways`) is a 404 there until
the [URL Rewrite module](https://www.iis.net/downloads/microsoft/url-rewrite) is installed and the `<rewrite>` rule in
`Web\web.config` is uncommented (it sends every path that is not a file to `index.html`); the page then calls the hub's
1967 by the host it was browsed from.

### Linux

1) `sudo apt install ./jde-opchub_<version>_amd64.deb` (Ubuntu 24.04 or later).  The hub runs as the `jde-opchub`
   systemd service (port 1967, a `jde-cpp` account); the OPC UA server is installed but not enabled:
   `sudo systemctl enable --now jde-opcserver`.  The database is sqlite under `/var/lib/Jde-Cpp/<Product>`, created on
   the first start.  Without root, the tarball's `install.sh` installs under your account and runs the products as
   `systemctl --user` units (`--opcserver` for the server).  Like the Windows current-user mode, it listens on
   `127.0.0.1` only.  To reach it from another machine, use the `.deb`, or set `listenAddress: null` in its
   `args/install-user` and open the ports.
2) Browse to http://localhost:1967/ - the hub serves the site; the packaged nginx site on 8071 is optional
   (`sudo ln -s /etc/jde-cpp/nginx-opchub.conf /etc/nginx/sites-enabled/jde-opchub && sudo systemctl reload nginx`).
3) Log in, as above - Google with the OPC UA Server component, otherwise a connection's own `<slug>\<user>`.
4) Uninstall: `sudo apt remove jde-opchub` (`./install.sh --uninstall` for a per-user install); the data under
   `/var/lib/Jde-Cpp` (`~/.config/Jde-Cpp`) is left in place.  `apt purge` also removes the nginx site's link from
   step 2; after `apt remove`, `sudo rm /etc/nginx/sites-enabled/jde-opchub` if you made it.

### First run

With the OPC UA Server component, `Jde.OpcServer` is the hub's default connection.  Gateways in the Web UI lists it
under the hub's gateway.  Browse to a node to see its value:

- Snapshot re-reads it.
- The checkbox beside it streams it.
- A typed value writes it, where the server reports the node writable for you.  A lock beside a cell means it is not.

Every value in the shipped address space is static.  To see the stream, open the node in two browser windows, tick the
box in one and type a value in the other.  The untouched window takes the new value through the subscription.

`Jde.Opc.PlcEmulator` ([`apps/OpcServer/emulator/README.md`](../OpcServer/emulator/README.md)) moves values by
itself.  Neither installer packages it; build it from the repo and point it at the server.

Any other OPC UA server is a connection added under `/apps` > the OpcHub card > Connections > Add.  That server may
need to trust the hub's client certificate the first time.

Roles are seeded, but the first grant is manual.  Every resource ships unenforced, so the first user can make it.

## Not done here

Soak support (`apps/OpcGateway/soak/soak.sh` is shaped around three exes), source consolidation under this directory.

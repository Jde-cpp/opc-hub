# Jde OpcHub - Linux package

`build-deb.sh` builds `jde-opchub_<version>_amd64.deb` and `jde-opchub-<version>-linux-amd64.tar.gz` from the release build
tree - the counterpart of the Windows installer (`../OpcHubSetup.nsi`, `../README.md`).  The `.deb` installs the hub, the
OPC UA server and the Web UI files system-wide, the products as systemd services under a `jde-cpp` account; the tarball is
the per-user install - `install.sh`, no root, `systemctl --user` units.  The database is sqlite in both.

## Building the package

Prerequisites on the build machine (Ubuntu 24.04 - the binaries need glibc 2.38, and both the `Depends:` names and the
`GLIBC_x.y` floor are resolved against the machine that builds it, so **build on the oldest release the package claims**;
the CI runner's container is `ubuntu-noble` for that reason):

| what | where |
|---|---|
| the release build tree | `$JDE_BUILD_DIR/$JDE_COMPILER/<repo dir>/release` (`--build-dir`), configured with `linux-clang-relWithDebInfo-jde`: the targets `Jde.Opc.Hub`, `Jde.Opc.Server`, `Jde.DB.Sqlite`, `Jde.DB.Sqlite.AppServer`, `Jde.DB.Sqlite.OpcGateway` |
| the Angular site | `web/opc/my-workspace/dist/my-workspace/browser` - `web/opc/scripts/setup.sh` runs `ng build` (`--web-dist`, or `--skip-web`); its `*.map` files are not packed.  `setup.sh --release` (the workflows' tag runs) hashes the output names, which the hub then serves as immutable; a plain `setup.sh` keeps `main.js` |
| [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset) | `$UA_NODE_SETS`, else `$REPO_DIR/UA-Nodeset` (`--ua-nodesets`) - DI/IA nodesets for the OpcServer |
| `dpkg-deb`, `binutils` | dpkg's own, `objdump`/`strip` (`apt install binutils`) |
| `patchelf` | `apt install patchelf`, or the PyPI wheel (`pip install patchelf`, then `--patchelf <path>`) - sets every staged exe's and `.so`'s RUNPATH to `$ORIGIN` |

```bash
apps/OpcHub/setup/linux/build-deb.sh                               # -> <BuildDir>/setup/jde-opchub_<JDE_VERSION>_amd64.deb + .tar.gz (CMakePresets.common.json - 2026.09.01)
apps/OpcHub/setup/linux/build-deb.sh --version 2026.09.08 --skip-web
apps/OpcHub/setup/linux/build-deb.sh --no-strip                    # keep the dwarf (file:line in the stack traces); several times the size
```

What it does: stages one dir per product with the exe, `libJde.so`, `libJde.DB.so`, the sqlite driver and the proc
modules, and beside them every `.so` those resolve outside the system dirs - fmt, Boost json/container, jsonnet from the
`$REPO_DIR` deps tree - plus LLVM's `libc++`/`libc++abi`, which the target distro ships an older major of, and `libxml2`
with the `libicuuc`/`libicudata` pair it links.  libxml2 is the one system library whose soname moves between the releases
this package claims: 24.04 builds against `libxml2.so.2`, 26.04 ships only `libxml2.so.16` (`libxml2-16`, and `libicu74`
-> `libicu78`), so a package built on the older one would not install on the newer, and forced past apt its
`Jde.Opc.Server` could not load at all.  Carrying the three costs ~8.6 MB compressed, nearly all of it `libicudata`.  Every staged
exe and `.so` gets `RUNPATH=$ORIGIN` (patchelf), so a product dir resolves by itself: our own are linked that way already
(`build/functions.cmake`, with the build tree's entries behind it), the third-party ones have no RUNPATH at all.
`Depends:` is what is left: the packages owning the system libraries the staged binaries still load (`libssl3t64`,
`zlib1g`, `libzstd1`, `liburing2`, `liblzma5`, `libstdc++6`, `libgcc-s1`), `libc6` at the highest `GLIBC_x.y` any of them imports,
`adduser` and `tzdata` (libc++'s chrono reads `/usr/share/zoneinfo`); `ca-certificates` is recommended, for the OS trust
store.  The version is `CMakePresets.common.json`'s `JDE_VERSION` - the string the C++ targets and the Web UI carry - unless
`--version` names one (the release workflow passes the tag, which should equal it): a `yyyy.MM.dd` as it is,
`yyyy.MM.dd-N-gsha` as `yyyy.MM.dd+N.gsha`.

CI: the Linux Release workflow (`.github/workflows/linux-release.yml`) builds the release tree on the self-hosted runner
(`.github/docker/`), the Web UI on a GitHub-hosted `web` job as the Windows workflow does, runs `build-deb.sh` and uploads
the `.deb` and the tarball as an artifact; a push of a `yyyy.MM.dd` tag runs it too and publishes both on that tag's GitHub
release, beside the Windows installer.

## Installing the `.deb`

```bash
sudo apt install ./jde-opchub_<version>_amd64.deb
```

- creates the `jde-cpp` system account, owner of `/var/lib/Jde-Cpp`;
- enables and starts `jde-opchub` (port 1967).  The OPC UA server is installed but not enabled - the hub can connect to
  any OPC UA server: `sudo systemctl enable --now jde-opcserver` (opc.tcp 4840, http 1970; requires the hub, as the
  Windows service's `depend=`).  The package opens no firewall port (the Windows installer does): with ufw or firewalld
  on, allow 1967/tcp, and 4840/tcp for the OPC UA server, yourself;
- the Web UI: the site file is installed, not enabled - `sudo ln -s /etc/jde-cpp/nginx-opchub.conf
  /etc/nginx/sites-enabled/jde-opchub && sudo systemctl reload nginx`, then http://127.0.0.1:8071 - optional: the hub serves the
  site itself at http://<host>:1967/ (`http.site` = `$(ExeDir)/../web`, the package's `/opt/jde-cpp/web`);
- `JDE_PASSCODE` (the private keys' passphrase, `$(JDE_PASSCODE)` in the configs): unset, the keys are written in the
  clear - the documented behaviour of an empty passcode.  Set it in `/etc/jde-cpp/env` (root-owned, `jde-cpp`-readable)
  before the first start.

The log is the journal - `journalctl -u jde-opchub -f` - and the files under the product dir.

## Installed layout

```
/opt/jde-cpp
  opchub/     Jde.Opc.Hub libJde.so libJde.DB.so libJde.DB.Sqlite.so libJde.DB.Sqlite.AppServer.so libJde.DB.Sqlite.OpcGateway.so
              libfmt.so.12 libboost_json.so.1.92.0 libboost_container.so.1.92.0 libjsonnet.so.0 libjsonnet++.so.0 libc++.so.1 libc++abi.so.1
  opcserver/  Jde.Opc.Server + the same, without the proc modules
  web/        the Angular site
/etc/jde-cpp                                             settings mirror - repo layout, so the configs' relative imports keep working (dpkg conffiles)
  apps/OpcHub/config/Opc.Hub.jsonnet                     (imports ../../AppServer/config/App.Server.jsonnet, ../../OpcGateway/config/Opc.Gateway.jsonnet)
  apps/OpcHub/config/args/install/args.libsonnet         sqlite; the driver/proc modules by $(ExeDir), the data by $(ProgramData)
  apps/AppServer/config/App.Server.jsonnet
  apps/OpcGateway/config/Opc.Gateway.jsonnet
  apps/OpcGateway/config/introspection/*.jsonnet
  apps/OpcServer/config/Opc.Server.jsonnet + Opc.Server.Install.jsonnet (the overlay the service loads)
  apps/OpcServer/config/args/install/args.libsonnet
  apps/OpcServer/config/pubsub/pumps.libsonnet
  libs/db/config/paths-common.libsonnet
  env                                                    the services' environment (JDE_PASSCODE)
  nginx-opchub.conf                                      the Web UI site, for /etc/nginx/sites-enabled
/var/lib/Jde-Cpp                                         the data root - $STATE_DIRECTORY's parent (Process::ProgramDataFolder), owned by jde-cpp
  OpcHub/                                                the product dir (Process::ProductName): created here by the service -> OpcHub.db, ssl/, *.log
    access-meta.jsonnet access-ql.jsonnet app-meta.jsonnet opcGateway-meta.jsonnet common-meta.libsonnet
    sql/  access.mutation (libs/access/config/release.mutation) app.mutation, the sqlite *_ql.sql views
          access_google.mutation, access_opcServer.mutation, gateway_opcServer.mutation - the OPC UA server's (libs/access/config/release-*.mutation,
          apps/OpcGateway/config/release-opcServer.mutation): the Web UI's Google provider, the server as the default connection with its provider row
  OpcServer/                                             OpcServer.db, ssl/, *.log
    access-meta.jsonnet access-ql.jsonnet common-meta.libsonnet opcServer-meta.jsonnet
    nodesets/ Opc.Ua.Di.NodeSet2.xml Opc.Ua.IA.NodeSet2.xml Opc.Ua.IA.NodeSet2.examples.xml pumps.NodeSet2.xml
/usr/lib/systemd/system/jde-opchub.service, jde-opcserver.service
```

The settings are edited in place under `/etc/jde-cpp`; the meta/sql files are what `args/install` points at.  The units
run the exes in the foreground (`-c`: the log goes to stdout, i.e. the journal - without it the exe `daemon()`s):

```
/opt/jde-cpp/opchub/Jde.Opc.Hub -c -settings=/etc/jde-cpp/apps/OpcHub/config/Opc.Hub.jsonnet -include=args/install -sync
/opt/jde-cpp/opcserver/Jde.Opc.Server -c -settings=/etc/jde-cpp/apps/OpcServer/config/Opc.Server.Install.jsonnet -include=args/install -sync
```

`-sync` creates the tables in the fresh `.db` on the first start and is idempotent afterwards (create-missing tables,
recreate the views, upsert the mutations); drop it later with `systemctl edit jde-opchub`.  The unit's
`StateDirectory=Jde-Cpp` is what puts the data root under `/var/lib`: the process reads `$STATE_DIRECTORY` and takes its
parent (`libs/fwk/src/process/os/linux/LinuxApp.cpp`); a process started without it - a console run, a `--user` unit -
uses `$XDG_CONFIG_HOME`, else `~/.config`.  The exe's `-install`/`-uninstall` are Windows-only; here the package owns the
registration.

## Per-user install (the tarball)

The counterpart of the Windows installer's "Current user" mode - no root, the products as `systemctl --user` units:

```bash
tar xzf jde-opchub-<version>-linux-amd64.tar.gz && cd jde-opchub-<version>-linux-amd64   # or wherever it was unpacked
./install.sh                # the hub (+ the Web UI files); enable and start jde-opchub
./install.sh --opcserver    # ... and the OPC UA server
./install.sh --uninstall
```

| | where |
|---|---|
| programs | `~/.local/share/jde-cpp/{opchub,opcserver,web}` (`$XDG_DATA_HOME`) |
| settings mirror | `~/.config/Jde-Cpp/config` - the same tree as `/etc/jde-cpp` |
| data | `~/.config/Jde-Cpp/<Product>` (`$XDG_CONFIG_HOME`) - what `Process::ProgramDataFolder()` returns for a user process |
| passcode | `~/.config/Jde-Cpp/env` (never overwritten) |
| units | `~/.config/systemd/user/jde-opchub.service`, `jde-opcserver.service`; `journalctl --user -u jde-opchub` |

The units run while the account is logged in; `loginctl enable-linger $USER` starts them at boot instead (the "Start at
logon" component).  The tarball also holds the system units under `usr/lib/systemd/system` for an install by hand on a
distro without dpkg: copy `opt`, `etc` and `var` to `/`, the units beside them, create the `jde-cpp` account and `chown -R`
the data root as `debian/postinst` does.

## Uninstall

`sudo apt remove jde-opchub` stops and disables the services, removes the program dirs, the units and the meta/sql/
nodesets the package put in the product dirs; `apt purge` removes `/etc/jde-cpp` as well.  Left in place, deliberately
- on purge too, as the Windows uninstaller leaves `%ProgramData%\Jde-Cpp`: `OpcHub.db`, `OpcServer.db`, `ssl/`
(certificates and keys - the OPC servers trust them), the logs, and the `jde-cpp` account that owns them.  Delete
`/var/lib/Jde-Cpp` by hand for a clean slate.  `./install.sh --uninstall` does the same for a per-user install, keeping
`~/.config/Jde-Cpp/<Product>`.

## Notes

- Upgrading (installing a newer `.deb` over the old one) restarts whatever was running; the `.db` is kept, `sql/` and
  `nodesets/` are replaced, and the settings under `/etc/jde-cpp` are conffiles - dpkg keeps an edited one and asks when
  the package's copy changed too.
- `apps/OpcGateway/config/access-opcGateway.mutation` (the gateway's group/role) is not seeded: `createGroup`/`createRole`
  run through the access server's QL, which is up only after the schema sync, so it is a post-start step, not a
  `dataPaths` seed.
- A split `Jde.AppServer` + `Jde.OpcGateway` pair (`apps/AppServer`, `apps/OpcGateway`) shares port 1967 with the hub; it
  is not packaged.
- The hub's web certificate (self-signed, issued on the first start) names `localhost`, the machine's name and `127.0.0.1`;
  `hostNames` in `/etc/jde-cpp/apps/OpcHub/config/args/install/args.libsonnet` adds the others a browser reaches the hub by, and
  `certificate:{ managed:false, path:… }` + `privateKey:{ path:…, passcode:… }` there use your own CA-issued pair as found -
  the Windows README's Notes have the details.  The Web UI uses plain HTTP on 1967 by ruling.
- First login: the package seeds the OPC UA server as the hub's default connection (with its provider row) and the Web UI's
  Google provider - the fresh install's login; by ruling no username or password is seeded.  `systemctl enable --now
  jde-opcserver`, then log in with Google: the site's origin must be registered under the OAuth client id the hub serves
  (`googleAuthClientId` in `apps/OpcHub/config/args/install/args.libsonnet`) - the Windows README's "First login" has the
  details.  The tarball's `install.sh` seeds the same with `--opcserver` and drops the seeds without it.
- Connecting the hub to another OPC UA server (`/apps/gateways`, Add): set the connection's Certificate URI to that server's
  application URI and the gateway opens a Sign & Encrypt session - Aes256_Sha256_RsaPss, Aes128_Sha256_RsaOaep or
  Basic256Sha256, the strongest the server shares - with a certificate it issues for the connection
  (`/var/lib/Jde-Cpp/OpcHub/ssl/certs/OpcHub.<slug>.pem` - labelled with the hub's own application URI, `urn:<machine>:Jde-Cpp:OpcHub`, which is how it introduces
  itself to every server; the Certificate URI is the server's and only selects its endpoints); an empty URI is an unsecured session - the data in the clear, the
  credential still encrypted to the server's certificate - for a server that publishes an unsecured endpoint at that URL.
  Trust is two-way and manual: copy the server's
  certificate into `/var/lib/Jde-Cpp/OpcHub/ssl/servers` (created on the first start; `gateway.trustedCertDirs` in
  `apps/OpcGateway/config/Opc.Gateway.jsonnet` - the servers the gateway talks to, a list apart from the certificates that may
  log in to the hub), and trust the hub's certificate above in the server's own trust list.  The Web UI's
  Gateways help topic (`?`) has the details.
- MySQL instead of sqlite, by hand: the driver builds on Linux (`libs/db/drivers/mysql`); an args profile like
  `apps/OpcHub/config/args/install-sqlServer/args.libsonnet` - the driver beside the exe, the `sql/mysql` scripts in the
  product's `sql/` - re-registered with `-include=args/install-mysql` through `systemctl edit`.
- Hardening in the units (`ProtectSystem=full`, `ProtectHome`, `PrivateTmp`, `NoNewPrivileges`): the process writes only
  under its `StateDirectory`.  Loosen with `systemctl edit` if a local change needs it.

# Jde OpcHub - Linux package

`build-deb.sh` builds two packages from the release build tree:

- `jde-opchub_<version>_amd64.deb`, the system-wide install.
- `jde-opchub-<version>-linux-amd64.tar.gz`, the per-user install.

It is the counterpart of the Windows installer (`../OpcHubSetup.nsi`, [`../README.md`](../README.md)).

The `.deb` installs the hub, the OPC UA server and the Web UI files system-wide. The products run as systemd services
under a `jde-cpp` account.

The tarball's `install.sh` installs per user. It needs no root and runs the products as `systemctl --user` units.

Both use sqlite for the database.

## Building the package

Prerequisites on the build machine (Ubuntu 24.04 - the binaries need glibc 2.38, and both the `Depends:` names and the
`GLIBC_x.y` floor are resolved against the machine that builds it, so **build on the oldest release the package claims**;
the CI runner's container is `ubuntu-noble` for that reason):

| what | where |
|---|---|
| the release build tree | `$JDE_BUILD_DIR/$JDE_COMPILER/<repo dir>/release` (`--build-dir`), configured with `linux-clang-relWithDebInfo-jde`: the targets `Jde.Opc.Hub`, `Jde.Opc.Server`, `Jde.DB.Sqlite`, `Jde.DB.Sqlite.AppServer`, `Jde.DB.Sqlite.OpcGateway`, `Jde.DB.MySql` |
| the Angular site | `web/opc/my-workspace/dist/my-workspace/browser` - `web/opc/scripts/setup.sh` runs `ng build` (`--web-dist`, or `--skip-web`); its `*.map` files are not packed.  `setup.sh --release` (the workflows' tag runs) hashes the output names, which the hub then serves as immutable; a plain `setup.sh` keeps `main.js` |
| [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset) | `$UA_NODE_SETS`, else `$REPO_DIR/UA-Nodeset` (`--ua-nodesets`) - DI/IA nodesets for the OpcServer |
| `dpkg-deb`, `binutils` | dpkg's own, `objdump`/`strip` (`apt install binutils`) |
| `patchelf` | `apt install patchelf`, or the PyPI wheel (`pip install patchelf`, then `--patchelf <path>`) - sets every staged exe's and `.so`'s RUNPATH to `$ORIGIN` |

```bash
apps/OpcHub/setup/linux/build-deb.sh                               # -> <BuildDir>/setup/jde-opchub_<version>_amd64.deb + .tar.gz (`git describe --tags`, else CMakePresets.common.json's JDE_VERSION)
apps/OpcHub/setup/linux/build-deb.sh --version 2026.09.08 --skip-web
apps/OpcHub/setup/linux/build-deb.sh --no-strip                    # keep the dwarf (file:line in the stack traces); several times the size
```

What it does:

It stages one dir per product with the exe, `libJde.so`, `libJde.DB.so`, the sqlite driver and the proc modules.

Beside them goes every `.so` those resolve outside the system dirs:

- fmt, abseil, Boost and jsonnet from the `$REPO_DIR` deps tree;
- LLVM's `libc++`/`libc++abi`, which the target distro ships an older major of;
- `libxml2` with the `libicuuc`/`libicudata` pair it links.

libxml2 is the one system library whose soname moves between the releases this package claims. 24.04 builds against
`libxml2.so.2`; 26.04 ships only `libxml2.so.16` (`libxml2-16`, and `libicu74` -> `libicu78`). A package built on
24.04 would not install on 26.04, and forced past apt its `Jde.Opc.Server` could not load. Carrying the three costs
~8.6 MB compressed, nearly all of it `libicudata`.

Every staged exe and `.so` gets `RUNPATH=$ORIGIN` (patchelf), so a product dir resolves by itself. Our own are linked
that way already (`build/functions.cmake`, with the build tree's entries behind it); the third-party ones have no
RUNPATH at all.

`Depends:` is what is left:

- the packages owning the system libraries the staged binaries still load (`libssl3t64`, `zlib1g`, `libzstd1`,
  `liburing2`, `liblzma5`, `libstdc++6`, `libgcc-s1`);
- `libc6` at the highest `GLIBC_x.y` any of them imports;
- `adduser`, and `tzdata` because libc++'s chrono reads `/usr/share/zoneinfo`.

`ca-certificates` is recommended, for the OS trust store.

Nothing installs a tarball's dependencies, so the tarball also carries the `Depends:` a system may lack. Today that is
`liburing2`'s `liburing.so.2`. `build-deb.sh` names both halves: `tarballPkgs` are carried, and `systemPkgs` (glibc,
the gcc runtime, openssl, compression) are left to the system, so its loader keeps its own glibc and apt keeps
updating openssl. A package in neither list fails the build.

The version is `git describe --tags` unless `--version` names one (the release workflow passes the tag):

- a `yyyy.MM.dd` tag is used as it is;
- `yyyy.MM.dd-N-gsha` becomes `yyyy.MM.dd+N.gsha`, which `dpkg` sorts after the tag, so a build past a release
  installs over it;
- with no tag reachable, it is `CMakePresets.common.json`'s `JDE_VERSION`, the string the C++ targets and the Web UI
  carry. `--version` is expected to agree with it.

CI: the Linux Release workflow (`.github/workflows/linux-release.yml`) builds the release tree on the self-hosted runner
(`.github/docker/`), the Web UI on a GitHub-hosted `web` job as the Windows workflow does, runs `build-deb.sh` and uploads
the `.deb` and the tarball as an artifact; a push of a `yyyy.MM.dd` tag runs it too and publishes both on that tag's GitHub
release, beside the Windows installer.

## Installing the `.deb`

```bash
sudo apt install ./jde-opchub_<version>_amd64.deb
```

- refuses to install on a CPU without the x86-64-v3 set plus PCLMUL and AES (`avx2`, `bmi1`, `bmi2`, `fma`, `f16c`,
  `movbe`, `abm`, `pclmulqdq`, `aes` in `/proc/cpuinfo`). In a VM, choose a CPU model that passes them through, such
  as `host`. The tarball's `install.sh` makes the same check;
- creates the `jde-cpp` system account, owner of `/var/lib/Jde-Cpp`;
- enables and starts `jde-opchub` (port 1967);
- installs the OPC UA server but does not enable it, since the hub can connect to any OPC UA server. To run it:
  `sudo systemctl enable --now jde-opcserver` (opc.tcp 4840, http 1970). It requires the hub, as the Windows service's
  `depend=` does;
- opens no firewall port (the Windows installer does). With ufw or firewalld on, allow 1967/tcp yourself, and 4840/tcp
  for the OPC UA server;
- the Web UI: the site file is installed, not enabled - `sudo ln -s /etc/jde-cpp/nginx-opchub.conf
  /etc/nginx/sites-enabled/jde-opchub && sudo systemctl reload nginx`, then http://127.0.0.1:8071 - optional: the hub serves the
  site itself at http://<host>:1967/ (`http.site` = `$(ExeDir)/../web`, the package's `/opt/jde-cpp/web`);
- `JDE_PASSCODE` is the private keys' passphrase (`$(JDE_PASSCODE)` in the configs). Unset, the keys are written in the
  clear, the documented behaviour of an empty passcode.

  To set it, edit `/etc/jde-cpp/env` (root-owned, `jde-cpp`-readable), then run
  `sudo systemctl restart jde-opchub jde-opcserver`. The install has already started the hub, so a key it wrote in the
  clear is encrypted at that restart. It is the same key, so no certificate or trust changes; the log says
  `Encrypted the private key at …`.

  A key written under one passcode does not open under another.

  The file is not a conffile. An upgrade never replaces it, and `apt purge` keeps it while it sets a passcode, since
  it keeps the keys that passcode opens.

The log is the journal - `journalctl -u jde-opchub -f` - and the files under the product dir.

## Installed layout

```
/opt/jde-cpp
  opchub/     Jde.Opc.Hub libJde.so libJde.DB.so libJde.DB.Sqlite.so libJde.DB.Sqlite.AppServer.so libJde.DB.Sqlite.OpcGateway.so
              libJde.DB.MySql.so libfmt.so.12 libboost_json.so.1.92.0 libboost_container.so.1.92.0 libboost_charconv.so.1.92.0
              libjsonnet.so.0 libjsonnet++.so.0 libabseil_dll.so.2605.0.0 libc++.so.1 libc++abi.so.1
  opcserver/  Jde.Opc.Server + the same, without the proc modules or the MySQL driver
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
  env                                                    the services' environment (JDE_PASSCODE) - laid from /usr/share/jde-opchub/env when absent
  nginx-opchub.conf                                      the Web UI site, for /etc/nginx/sites-enabled
/var/lib/Jde-Cpp                                         the data root - $STATE_DIRECTORY's parent (Process::ProgramDataFolder), owned by jde-cpp
  OpcHub/                                                the product dir (Process::ProductName): created here by the service -> OpcHub.db, ssl/, *.log
    access-meta.jsonnet access-ql.jsonnet app-meta.jsonnet opcGateway-meta.jsonnet common-meta.libsonnet
    sql/  access.mutation, access.roles (libs/access/config/release.*), app.mutation, the sqlite *_ql.sql views
          access_google.mutation, access_opcServer.mutation, access_opcServer.roles, gateway_opcServer.mutation - the
          OPC UA server's (libs/access/config/release-*, apps/OpcGateway/config/release-opcServer.mutation): the Web
          UI's Google provider, the server as the default connection with its provider row, the OPC Server Instance role
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
tar xzf jde-opchub-<version>-linux-amd64.tar.gz && cd jde-opchub-<version>-linux-amd64   # the archive's one top-level directory
./install.sh                # the hub (+ the Web UI files); enable and start jde-opchub
./install.sh --opcserver    # ... and the OPC UA server
./install.sh --uninstall
```

It needs no package installed first: the tarball carries the libraries a system may lack, `liburing.so.2` among them, which
the `.deb` gets from apt.  `install.sh` checks with `ldd` that everything the products load resolves, and if not, it names the
missing library and stops before copying anything.

Rerun `./install.sh` from a newer tarball to upgrade, or with `--opcserver` to add the OPC UA server later: it replaces
the programs while they run and restarts the products onto the new files (a running jde-opcserver restarts with the
hub).  The settings under `~/.config/Jde-Cpp/config` are replaced too - but for an `args/install*/args.libsonnet` you
edited, which stays in use with this release's copy beside it as `args.libsonnet.new` (the script names each one - merge
any change by hand), as the `.deb` does with its conffiles.

| | where |
|---|---|
| programs | `~/.local/share/jde-cpp/{opchub,opcserver,web}` (`$XDG_DATA_HOME`) |
| settings mirror | `~/.config/Jde-Cpp/config` - the same tree as `/etc/jde-cpp`; `.dist/` beside it is what the last `install.sh` laid, how a rerun tells your edits from its own copies |
| data | `~/.config/Jde-Cpp/<Product>` (`$XDG_CONFIG_HOME`) - what `Process::ProgramDataFolder()` returns for a user process |
| passcode | `~/.config/Jde-Cpp/env` (never overwritten) |
| addresses | loopback only - the hub on `127.0.0.1:1967`, the server on `127.0.0.1:1970` and `opc.tcp://127.0.0.1:4840` (below) |
| units | `~/.config/systemd/user/jde-opchub.service`, `jde-opcserver.service`; `journalctl --user -u jde-opchub` |

The addresses are loopback only because the units run `-include=args/install-user`, whose `listenAddress` binds them.
An install that needs no root cannot open a firewall port, so it does not publish one. The `.deb` binds every
interface, where the administrator who installed it decides.

To reach this install from another machine, use the `.deb`. Or set `listenAddress: null` in
`~/.config/Jde-Cpp/config/apps/{OpcHub,OpcServer}/config/args/install-user/args.libsonnet`, allow the ports in the
firewall and run `systemctl --user restart jde-opchub jde-opcserver`.

The units run while the account is logged in; `loginctl enable-linger $USER` starts them at boot instead (the "Start at
logon" component).  The tarball also holds the system units under `usr/lib/systemd/system` for an install by hand on a
distro without dpkg: copy `opt`, `etc` and `var` to `/`, the units beside them, create the `jde-cpp` account and `chown -R`
the data root as `debian/postinst` does.

## Uninstall

`sudo apt remove jde-opchub` stops and disables the services (`apt install` again enables and starts the ones that were
enabled), removes the program dirs, the units and the meta/sql/nodesets the package put in the product dirs; `apt purge`
removes `/etc/jde-cpp` as well - but for an `env` that sets `JDE_PASSCODE`.

Left in place, deliberately - on purge too, as the Windows uninstaller leaves `%ProgramData%\Jde-Cpp`: `OpcHub.db`,
`OpcServer.db`, `ssl/` (certificates and keys - the OPC servers trust them), the logs, and the `jde-cpp` account that
owns them - with the passcode in `/etc/jde-cpp/env` that opens those keys.  Delete `/var/lib/Jde-Cpp` and `/etc/jde-cpp`
by hand for a clean slate.

After the *MySQL instead of sqlite* switch (Notes), its `systemctl edit` drop-in, `/etc/systemd/system/jde-opchub.service.d/`,
stays too:  delete it by hand as well, then `sudo systemctl daemon-reload`.  Left behind, it starts the next install on
the profile the purge removed, and the hub restarts every 5 s with `couldn't open import "args.libsonnet"`;
`sudo systemctl revert jde-opchub` clears it then.

A database is its `.db` with any `.db-wal`/`.db-shm` beside it:  a clean stop folds them back into the `.db` and deletes
them, but after a crash or a `kill -9` the latest rows are still in the `-wal` - copy, move or delete the three together.

`./install.sh --uninstall` does the same for a per-user install, keeping `~/.config/Jde-Cpp/<Product>`.

The Web UI site's link into nginx (`/etc/nginx/sites-enabled/jde-opchub`, or any `sites-enabled`/`conf.d` link to
`/etc/jde-cpp/nginx-opchub.conf`) goes with `apt purge`, and nginx is reloaded; after a plain `apt remove` the link stays
valid (the conffile is kept), so nginx still loads, but 8071 answers 404 until the package is back - `sudo rm` it by hand
to drop the 8071 site.

## Notes

- Upgrading (installing a newer `.deb` over the old one) restarts whatever was running; the `.db` is kept, `sql/` and
  `nodesets/` are replaced, and the settings under `/etc/jde-cpp` are conffiles - dpkg keeps an edited one and asks when
  the package's copy changed too.  When the Web UI site is enabled, nginx is reloaded as well, so a new site file is live
  at once; if `nginx -t` rejects the config, the upgrade says so and leaves nginx on the one it has.
- `apps/OpcGateway/config/access-opcGateway.mutation` (the gateway's group/role) is not seeded: `createGroup`/`createRole`
  run through the access server's QL, which is up only after the schema sync, so it is a post-start step, not a
  `dataPaths` seed.
- A split `Jde.AppServer` + `Jde.OpcGateway` pair (`apps/AppServer`, `apps/OpcGateway`) is not packaged. Its AppServer
  listens on 1967, the hub's port, and its gateway on 1968, so stop the hub before running the pair on this machine.
- The hub's web certificate (self-signed, issued on the first start) names `localhost`, the machine's name and `127.0.0.1`;
  `hostNames` in `/etc/jde-cpp/apps/OpcHub/config/args/install/args.libsonnet` adds the others a browser reaches the hub by, and
  `certificate:{ managed:false, path:… }` + `privateKey:{ path:…, passcode:… }` there use your own CA-issued pair as found -
  the Windows README's Notes have the details.  The Web UI uses plain HTTP on 1967 by ruling.
- First login: the package seeds the OPC UA server as the hub's default connection (with its provider row) and the Web UI's
  Google provider - the fresh install's login; by ruling no username or password is seeded.  `systemctl enable --now
  jde-opcserver`, then log in with Google: the site's origin must be registered under the OAuth client id the hub serves
  (`googleAuthClientId` in `apps/OpcHub/config/args/install/args.libsonnet`) - the Windows README's "First login" has the
  details.  The tarball's `install.sh` seeds the same with `--opcserver` and drops the seeds without it.
- Connecting the hub to another OPC UA server (`/apps` > the OpcHub card > Connections > Add): set the connection's
  Certificate URI to that server's application URI and the gateway opens a Sign & Encrypt session -
  Aes256_Sha256_RsaPss, Aes128_Sha256_RsaOaep or Basic256Sha256, the strongest the server shares - with a certificate it
  issues for the connection (`/var/lib/Jde-Cpp/OpcHub/ssl/certs/OpcHub.<slug>.pem` - labelled with the hub's own
  application URI, `urn:<machine>:Jde-Cpp:OpcHub`, which is how it introduces itself to every server; the Certificate
  URI is the server's and only selects its endpoints); an empty URI is an unsecured session - the data in the clear, the
  credential still encrypted to the server's certificate - for a server that publishes an unsecured endpoint at that
  URL. Trust is two-way and manual: copy the server's certificate into `/var/lib/Jde-Cpp/OpcHub/ssl/servers` (created on
  the first start; `gateway.trustedCertDirs` in `apps/OpcGateway/config/Opc.Gateway.jsonnet` - the servers the gateway
  talks to, a list apart from the certificates that may log in to the hub), and trust the hub's certificate above in the
  server's own trust list.  The Web UI's Gateways help topic (`?`) has the details.
- MySQL instead of sqlite, by hand: `apps/OpcHub/config/args/install-mysql/args.libsonnet` is the profile, and its
  driver, `libJde.DB.MySql.so`, is installed beside the exe.  The package ships only the driver, so take the profile and
  the scripts below from the repository at the installed version's tag (`dpkg-query -W jde-opchub` prints it).  The
  profile imports `args/install` and replaces only the database, so the Web UI, its Google client id and the host names
  stay that file's.
  1. Copy the profile to `/etc/jde-cpp/apps/OpcHub/config/args/install-mysql/`.  Its `host` and `port` name the server -
     `localhost` and 3306 as shipped - and `ssl` the transport: `enable` (as shipped) uses TLS when the server offers
     it and plaintext when it does not, `require` refuses a server without TLS, `disable` never negotiates it.
  2. Create a database `jde` on that server and a login with all privileges on it.  Set that login as `JDE_MYSQL_USER`
     and `JDE_MYSQL_PWD` in `/etc/jde-cpp/env`.
  3. Let the login create the gateway's trigger.  MySQL 8 turns binary logging on, and with it on, creating a trigger
     needs `SET_ANY_DEFINER` (or SUPER); without it the hub's first start exits on error 1419 and
     restarts every 5 s.  As MySQL's root, either
     `GRANT SET_ANY_DEFINER ON *.* TO <login>` (MySQL 8.2 and later) or `SET PERSIST log_bin_trust_function_creators = ON`
     (every 8.x, but deprecated since 8.0.34 and it warns when set).
  4. Copy the `config/sql/mysql/*.sql` scripts of `libs/access`, `apps/AppServer` and `apps/OpcGateway` into
     `/var/lib/Jde-Cpp/OpcHub/sql-mysql/` - the profile's `scriptPaths`, not `sql/`, which is the package's, its sqlite
     scripts replaced on every upgrade.
  5. Re-register with `-include=args/install-mysql` through `systemctl edit jde-opchub`: under `[Service]`, an
     `ExecStart=` reset, then the unit's line with the new `-include`.  For a MySQL on this machine, add
     `After=mysql.service` under `[Unit]`; systemd ignores it in `[Service]`.
  6. `sudo systemctl restart jde-opchub`.  `systemctl edit` restarts nothing, and the hub stays on sqlite until it
     restarts.  It starts on MySQL, and its first `-sync` makes the tables in `jde`.
  - For a MySQL on another machine, set `ssl: "require"` and the session is encrypted end to end.  Under `enable` a
    server without TLS still connects, and the user name, the SQL and the rows then cross the network in the clear
    (the hub warns once in its log); the password does not: MySQL's login sends it RSA-encrypted or scrambled.  No
    mode checks the server's certificate, so TLS hides the traffic from the network, not from an impostor on the port.
    `enable` and `disable` are for a trusted network only.
  - `apt reinstall` keeps the switch.  To go back to sqlite, `sudo systemctl revert jde-opchub` removes the drop-in, and
    `sudo systemctl restart jde-opchub` starts the hub on `-include=args/install` again.
- Hardening in the units (`ProtectSystem=full`, `ProtectHome`, `PrivateTmp`, `NoNewPrivileges`): the process writes only
  under its `StateDirectory`.  Loosen with `systemctl edit` if a local change needs it.

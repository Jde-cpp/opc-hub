# Jde OpcHub - Windows installer

The Linux package - a `.deb` with systemd units, and a per-user tarball - is [`linux/`](linux/README.md) beside this.

`OpcHubSetup.nsi` builds `OpcHubSetup-<version>.exe` (NSIS 3.11, Modern UI 2, `MultiUser.nsh`).  It installs the hub, optionally
the OPC UA server and the Web UI files, as Windows services or as a per-user install without administrator rights, with
sqlite as the database.

## Building the installer

Prerequisites on the build machine:

| what | where |
|---|---|
| NSIS 3.x | `C:\Program Files (x86)\NSIS` (`-MakeNsis` otherwise) |
| the release build tree | `$env:JDE_RBUILD_DIR\clang++\<repo dir>\release` (`-BuildDir`): `bin\Jde.Opc.Hub\`, `bin\Jde.Opc.Server\` and, in `bin\`, `Jde.DB.Sqlite.dll`, `sqlite3.dll`, `Jde.DB.Sqlite.AppServer.dll`, `Jde.DB.Sqlite.OpcGateway.dll` |
| the Angular site | `web\opc\my-workspace\dist\my-workspace\browser` - `web/opc/scripts/setup.sh` runs `ng build` (`-WebDist`, or `-SkipWeb`); its `*.map` files are not packed.  `setup.sh --release` (the workflows' tag runs) hashes the output names, which the hub then serves as immutable; a plain `setup.sh` keeps `main.js` |
| [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset) | `$env:UA_NODE_SETS` (`-UaNodeSets`) - DI/IA nodesets for the OpcServer |
| `vc_redist.x64.exe` | the VS 2026 install's `VC\Redist\MSVC\v145\` (`-VcRedist`), or https://aka.ms/vs/18/release/vc_redist.x64.exe - 14.50 or later, the installer's gate: the exes are built with the 14.51 toolset and Microsoft's rule is a redistributable at least as new as the toolset (the VS 2022 line's 14.44 happens to export every symbol they import, checked 09-12, but only by luck); bundled for the all-users mode, skipped with a warning if missing |

```powershell
.\build-setup.ps1                              # -> <BuildDir>\setup\OpcHubSetup-<JDE_VERSION>.exe (CMakePresets.common.json's JDE_VERSION)
.\build-setup.ps1 -Version 2026.09.08 -SkipWeb
.\build-setup.ps1 -Sign -PfxPath <cert.pfx>    # signed with a .pfx; -Sign alone uses Azure Artifact Signing - see Signing
```

Every input is a `/D` define of the script, so `makensis /DBUILD_DIR=… OpcHubSetup.nsi` works without the wrapper.

CI: the Win2025 workflow (`.github/workflows/win2025-build.yml`) runs `build-setup.ps1` after its release build - the
nodesets and `vc_redist.x64.exe` are downloaded, the Web UI comes from the workflow's `web` job (an `ubuntu-latest` run of
`web/opc/scripts/setup.sh`) - and uploads `OpcHubSetup-<version>.exe` as an artifact; a push of a `yyyy.MM.dd` tag runs it
too and publishes the installer as that tag's GitHub release.  Unsigned - no certificate route yet (Signing, below).

## Signing

`build-setup.ps1 -Sign` Authenticode-signs everything that ships - the exes and dlls before makensis packs them, the
uninstaller from inside makensis (`!uninstfinalize` in the `.nsi`: the uninstaller is generated at install time from a stub
built there, so nothing else can sign it) and the installer after - all through [`sign.ps1`](sign.ps1), which takes its
certificate from the environment (the hook's child process gets nothing else):

| certificate | settings | notes |
|---|---|---|
| Azure Artifact Signing | `JDE_SIGN_ENDPOINT` (the account's region, e.g. `https://eus.codesigning.azure.net`), `JDE_SIGN_ACCOUNT`, `JDE_SIGN_PROFILE` | public trust, the key in Microsoft's HSM; `Invoke-ArtifactSigning` (`Install-Module ArtifactSigning`, for the PowerShell that runs `build-setup.ps1`) with whatever Azure credential the process has - `az login` on a dev box, an azure/login OIDC session on a runner; timestamped by Microsoft |
| a `.pfx` | `JDE_SIGN_PFX` (`-PfxPath`), `JDE_SIGN_PFX_PASSWORD` | signtool from the Windows SDK (`JDE_SIGN_TOOL` overrides the path); a self-signed certificate (`New-SelfSignedCertificate -Type CodeSigningCert`) proves the pipeline end to end and earns no trust anywhere |

CI builds unsigned: no certificate route has been settled.  Azure Artifact Signing was tried on 2026-09-12 and is closed to
this project - its individual identity validation runs through AU10TIX's Verified ID, which would not verify, and the
organization route sends its representative through the same step.  Still open: SignPath Foundation (free for OSS; a "Code
signing policy" page and an application they review; signs the installer on their servers, never the uninstaller), an
individual OV certificate from a CA that validates individuals (SSL.com's eSigner has a hosted-runner GitHub Action; Certum's
open-source certificate signs on a dev box or the self-hosted runner), or an EV certificate, which needs a registered
business.  Until one is chosen, `-Sign -PfxPath` with a self-signed certificate (`New-SelfSignedCertificate -Type
CodeSigningCert`, then `Export-PfxCertificate`) exercises the whole pipeline on your own machines; the
`windows-release-binaries` artifact is the raw build tree either way.

SmartScreen: a public-trust certificate takes "Unknown publisher" off the UAC prompt at once; the "Windows protected your PC"
interstitial fades as the certificate accrues download reputation, which a new one starts without.

## Install modes

| | All users | Current user |
|---|---|---|
| rights | administrator (UAC prompt) | none - a standard user never sees a prompt, nor the mode page: Setup picks this mode for them and says so at the top of *Choose Components* (for services, run Setup as administrator - right-click); an administrator sees the mode page and may still pick this mode |
| program dir | `C:\Program Files\Jde-Cpp` | `%LOCALAPPDATA%\Programs\Jde-Cpp` |
| how the products run | Windows services `Jde.OpcHub`, `Jde.OpcServer` (auto start; `net start`/`net stop`), both running as **Local Service** - an unprivileged account, as the `.deb`'s `jde-cpp` is, not LocalSystem - the finish page's "Start now" box starts them at once, and its link is the Web UI's url | Start Menu folder `Jde-Cpp`: a shortcut per product, each a console window (`-c`) - the finish page's "Start now" box opens them at once, and its link is the Web UI's url; optional "Start at logon" component (HKCU Run) |
| firewall | inbound TCP 1967 allowed, and 4840 with the OPC UA Server component, on every profile (`netsh advfirewall`, rules named `Jde OpcHub (TCP 1967)` / `Jde OpcServer (TCP 4840)`; removed on uninstall) - a browser or an OPC client on another machine reaches the products.  Every profile because a new network lands in Public unless someone says otherwise, and 1967 is plain http with a login on it, by ruling | none needed: this mode's `-include=args/install-user` binds the listeners to loopback (`listenAddress: "127.0.0.1"`), so Windows raises no firewall prompt - one a standard user could only answer with an administrator's credentials - and the products answer this machine only.  For another machine: install for all users, or `listenAddress: null` there and an administrator's inbound rule |
| VC++ v14 x64 runtime, 14.50 or later | installed, or upgraded when older; when its installer wants a restart (its files were in use) the finish page says so and offers it - the services start with Windows after it, and "Start now" is not offered | when missing or older, Setup offers to run the bundled redistributable - it is machine-wide, so Windows asks for an administrator - and installs the products either way; while the runtime is still old, the finish page says they may fail to start and where the redistributable is.  (They ran on 14.40 through a whole walk; the gate is Microsoft's rule, not a measured floor.) |
| Add/Remove Programs | HKLM | HKCU (`Jde OpcHub (current user)`) |
| data | `C:\ProgramData\Jde-Cpp\<Product>` in both modes - the apps hardcode it (`Process::ProgramDataFolder()`, `libs/db/config/paths-common.libsonnet`).  Setup makes the tree SYSTEM's, the Administrators' and the services' alone: owner Administrators, nothing inherited from `%ProgramData%`, no entry for Users; Local Service reads it and may change `OpcHub\` and `OpcServer\` (the `.db`, `ssl\`, the logs).  The services' settings, keys and databases are no other account's to read or change, so read the logs or edit the settings from an elevated editor.  When another account already has files there - a current-user install of theirs - Setup names the first one and asks before taking them over (a silent install stops instead), and it refuses a link another account made. | The tree must be this install's to write:  Setup opens a file it is about to overwrite - the hub's config, else its `.db` - for writing, and refuses the current-user mode when it cannot, since the tree then belongs to another account's install.  An all-users install leaves a mark, `C:\ProgramData\Jde-Cpp\.all-users`, and Setup refuses this mode on it whoever runs Setup, an administrator included: the two modes do not share a data root.  After uninstalling the all-users install, delete the folder (as an administrator) before a current-user install. |

Silent: `OpcHubSetup-<v>.exe /S /AllUsers` or `/CurrentUser`, `/Start` to start the products at the end (the finish page's box,
which `/S` never shows), `/OpcServer` to add the OPC UA Server component (there is no
components page to pick it on), `/D=<dir>` for the program dir.  Exit code 3010 (msiexec's) means the runtime's installer
needs a restart: the products were not started and the services come up with Windows after it; nothing restarts the
machine by itself.

## Components

| component | section | ships |
|---|---|---|
| OPC Hub (`Jde.OpcHub`) | required | `Jde.Opc.Hub.exe` - the AppServer and the OpcGateway in one process, port 1967 |
| OPC UA Server (`Jde.OpcServer`) | optional, off | `Jde.Opc.Server.exe` - opc.tcp 4840, http 1970, DI/IA nodesets + the pumps demo address space; logs in to the hub with its certificate.  With it, the hub's seeds for it - the server as the default connection, its provider row, the Web UI's Google provider (First login, below) |
| Web UI | optional, on | the Angular site under `<program dir>\Web`, served by the hub itself at `http://<host>:1967/` (`Opc.Hub.jsonnet` `http.site`, `$(ExeDir)/../web` from the install args - the page and its api on one origin, no IIS). `web.config` is included for anyone who prefers the site behind IIS (`apps/OpcHub/README.md`, its IIS paragraph - deep links there need the URL Rewrite module). The page reaches the hub by the host it was browsed from |
| Start at logon | current-user only | HKCU Run entries for the selected products |

## Installed layout

```
<program dir>                                            C:\Program Files\Jde-Cpp  |  %LOCALAPPDATA%\Programs\Jde-Cpp
  OpcHub\     Jde.Opc.Hub.exe Jde.dll Jde.DB.dll fmt.dll z.dll libcrypto-3-x64.dll libssl-3-x64.dll
              Jde.DB.Sqlite.dll sqlite3.dll Jde.DB.Sqlite.AppServer.dll Jde.DB.Sqlite.OpcGateway.dll
  OpcServer\  Jde.Opc.Server.exe + the same + libxml2.dll, Jde.DB.Sqlite.dll sqlite3.dll
  Web\        the Angular site, served by the hub at http://<host>:1967/ (+ web.config for IIS, optional)
  LICENSE.txt THIRD-PARTY-NOTICES.txt                     ours (MIT), and the notices of the third-party code inside the exes and dlls
  Uninstall.exe
C:\ProgramData\Jde-Cpp
  config\                                                settings mirror - repo layout, so the configs' relative imports keep working
    apps\OpcHub\config\Opc.Hub.jsonnet                   (imports ../../AppServer/config/App.Server.jsonnet, ../../OpcGateway/config/Opc.Gateway.jsonnet)
    apps\OpcHub\config\args\install\args.libsonnet       sqlite; the driver/proc modules by $(ExeDir), the data by $(ProgramData)
    apps\OpcHub\config\args\install-user\args.libsonnet  the current-user mode's: args/install plus listenAddress 127.0.0.1
    apps\AppServer\config\App.Server.jsonnet
    apps\OpcGateway\config\Opc.Gateway.jsonnet
    apps\OpcGateway\config\introspection\*.jsonnet
    apps\OpcServer\config\Opc.Server.jsonnet + Opc.Server.Install.jsonnet (the overlay the service loads)
    apps\OpcServer\config\args\install\args.libsonnet
    apps\OpcServer\config\args\install-user\args.libsonnet  as the hub's
    apps\OpcServer\config\pubsub\pumps.libsonnet
    libs\db\config\paths-common.libsonnet
  OpcHub\                                                the product dir (Process::ProductName): created here by the service -> OpcHub.db, ssl\, *.log
    access-meta.jsonnet access-ql.jsonnet app-meta.jsonnet opcGateway-meta.jsonnet common-meta.libsonnet
    sql\  access.mutation (libs/access/config/release.mutation) access.roles (libs/access/config/release.roles) app.mutation, the sqlite *_ql.sql views
          with the OPC UA Server: access_google.mutation, access_opcServer.mutation (libs/access/config/release-google.mutation, release-opcServer.mutation),
          gateway_opcServer.mutation (apps/OpcGateway/config/release-opcServer.mutation)
  OpcServer\                                             OpcServer.db, ssl\, *.log
    access-meta.jsonnet access-ql.jsonnet common-meta.libsonnet opcServer-meta.jsonnet
    nodesets\ Opc.Ua.Di.NodeSet2.xml Opc.Ua.IA.NodeSet2.xml Opc.Ua.IA.NodeSet2.examples.xml pumps.NodeSet2.xml
```

The settings are edited in place under `config\`; the meta/sql files are what `args/install` points at.  The service
command lines (composed by the exe's `-install`, `libs/fwk/src/process/process.cpp` - `sc qc Jde.OpcHub` shows them):

```
"C:\Program Files\Jde-Cpp\OpcHub\Jde.Opc.Hub.exe" -settings=C:\ProgramData\Jde-Cpp\config\apps\OpcHub\config\Opc.Hub.jsonnet -include=args/install -sync
"C:\Program Files\Jde-Cpp\OpcServer\Jde.Opc.Server.exe" -settings=C:\ProgramData\Jde-Cpp\config\apps\OpcServer\config\Opc.Server.Install.jsonnet -include=args/install -sync
```

The current-user shortcuts are the same lines with `-c` in front.  `-sync` creates the tables in the fresh `.db` on the
first start and is idempotent afterwards (create-missing tables, recreate the views, upsert the mutations); drop it later
with `sc config Jde.OpcHub binPath= "…"` or by editing the shortcut.

## First login

There are two ways in, and which you get depends on the **OPC UA Server** component.

**With the component, the login is Google** and it needs nothing set up
([`reviews/install-issues.md`](../../../../reviews/install-issues.md) #1 - by ruling no username or password is seeded, and
Google is seeded only with this component): the component seeds the Google provider (`access_google.mutation`)
and `Jde.OpcServer` as the hub's default server connection (`gateway_opcServer.mutation`: slug `OpcServer`,
`opc.tcp://127.0.0.1:4840`; `access_opcServer.mutation`: its provider row).  The button works only from an origin registered
under the OAuth client id the hub serves (`GET /GoogleAuthClientId`).  The default is the project's own client id, so a
browser on the hub machine at `http://localhost:1967` works once that origin is registered on it; any other host needs its
own (Google Cloud console > APIs & Services > Credentials > OAuth client ID, Web application, authorized JavaScript origin
`http://<host>:1967`), put in `config\apps\OpcHub\config\args\install\args.libsonnet` (`googleAuthClientId`), then
`Jde.OpcHub` restarted.  The first grant is manual by ruling (the seeded roles); every resource ships unenforced, so the first
user can make it.

**Without it, the login is a server connection's own, and adding the connection is what creates it.**  The page's
username/password form signs in against *an OPC server the hub connects to* - any of them, not the bundled one - so it
works on a hub that has no bundled server at all.  Add the connection first, signed out (every resource ships unenforced,
so an anonymous write is permitted by design), exchange certificates with that server, then sign in.  The username is
**`<connection slug>\<user on that server>`** - `plant1\operator1` - and **give the prefix**: the slug is what selects
the connection.  [`login-page.ts`](../../../web/framework/control/src/lib/pages/authorization/login-page/login-page.ts) splits the username on the backslash and puts the slug in the request's
`opc` field; without one the hub tries its *default* connection - the bundled server, when that component is installed - and
otherwise refuses the sign-in with *"No default OPC server connection."* ([`ConnectAwait::ResolveDefault`](../../OpcGateway/src/async/ConnectAwait.cpp#L57)).  The connection's
insert is the whole mechanism - it creates an `OpcServer` provider row for the slug, and the first sign-in creates the
user.  The Web UI writes this up as *First steps* in its own
[Overview help](../../../web/opc/site/assets/help/overview.md), which is the copy an operator actually meets.

The **bundled** server is one such connection, and it offers the username token only when its settings list users
(`/opc/users: [{name, password}]` under `config\apps\OpcServer\config\` - an opt-in, nothing shipped sets it).  Its slug
is `OpcServer`, so its form login is `OpcServer\<name>` like any other; it is also the seeded default connection, so a bare `<name>` reaches it too.

Walked end to end on 2026-09-20 against a KEPServerEX 6.12 on a hub installed **without** the component: connection added,
trust exchanged, signed in as `kepware\Administrator` 11 minutes in, a node value streaming at 13
([`reviews/install-issues.md`](../../../../reviews/install-issues.md), "The login the product is for").

## Uninstall

Add/Remove Programs (or the Start Menu shortcut in a current-user install) stops and deregisters the services (or ends the
console windows and removes the shortcuts/Run entries), removes the program dir, the `config\` mirror and the meta/sql/
nodesets the installer put in the product dirs.  Left in place, deliberately: `OpcHub.db`, `OpcServer.db`, `ssl\`
(certificates and keys - the OPC servers trust them) and the logs.  Delete `C:\ProgramData\Jde-Cpp` by hand for a clean slate.

## Notes

- Reinstalling over an existing install is fine: the services are deregistered and re-registered, the `.db` is kept, the
  installer-owned `sql\` and `nodesets\` are recreated (the settings under `config\` are overwritten - keep a copy of edits).
  There is no need to stop anything first.  In **all users** mode Setup stops and deregisters `Jde.OpcServer` and
  `Jde.OpcHub` - the server first, since it depends on the hub - and waits for their processes to go **before it copies
  a file**, because Windows will not replace a running image; if a service will not stop within twenty seconds Setup
  says so and ends, rather than lay new settings and seeds over an exe it could not replace
  ([`reviews/m2-closing.md`](../../../../reviews/m2-closing.md) #4).  A `Jde.OpcServer` an earlier install registered is
  stopped with the hub even when the *OPC UA Server* component is left unticked; it stays registered on its old files -
  `net start Jde.OpcServer`, or tick the component.  A reinstall into a different program folder (the directory page offers the previous one) deregisters the services through the previous folder's exes and registers the new ones - the old folder is left behind, delete it; a registration Setup cannot remove, or an `-install` that fails, stops Setup with the exe's exit code rather than carrying on over the old registration ([`reviews/m4-closing.md`](../../../../reviews/m4-closing.md) #7).
- **A reinstall - and adding a component to one - only takes effect once the products restart.**  The seeds a component
  brings (`<schema>*.mutation`, `*.roles`) are applied by a `-sync` start, so a hub that keeps running through the
  install shows none of them: no Google provider, no `OpcServer` connection, no *OPC Server Instance* role, and pages
  that look exactly as they did before ([`reviews/install-issues.md`](../../../../reviews/install-issues.md) #37).  In
  **all users** mode Setup stops the services before it copies anything and `-Services` re-registers them, so the next start has them.  In **current user**
  mode there is no service to stop, so Setup closes a running `Jde.OpcHub` / `Jde.OpcServer` of yours first - it asks
  before it does, since these are console windows you opened (a silent install closes them without asking) - and the
  finish page's *Start now* box, or the Start Menu shortcut, brings the product back on the new files and the new seeds.
  Cancel the prompt to close them yourself and run Setup again.  Setup goes on only once it has *seen* the copy gone: it
  asks the window to close, ends it after ten seconds, and if it is still there five seconds later - a copy you started
  elevated, say - Setup stops and names what to close, rather than lay new settings over files it could not replace.
- Roles are seeded by a second pass: `<schema>.roles` files under `dataPaths` are upserted after the access server is
  configured (`createRole`/`addRole` run through its mutations, which the `.mutation` pass runs too early for).
  `release.roles` ships Viewer, System Administrator, Owner, Engineer, Operator and Maintenance Technician; `addRole` names
  roles by `slug`.  Each file is recorded (`access_seeds`, by content) once it applies, and a later `-sync` start skips it
  while it is unchanged, so an administrator's edits to a seeded role - a changed right, a deny, a removed permission or
  child role - survive restarts.  A release whose seed changed applies it again and adds only what a role is missing: it
  never rewrites an existing grant, but it does restore a permission or child role the administrator had removed.  Nor does it rewrite a seeded row's `create*` text - a role's name and description, the seeded connection's url: a changed seed reaches new rows only, so an install upgraded from 2026.09.02 keeps that release's Engineer, Operator and Maintenance Technician descriptions (edit them on Access > Roles; the current text is in `libs/access/config/release.roles`).
  `apps/OpcGateway/config/access-opcGateway.mutation`
  (the gateway's group/role) is still not seeded: its `createRole( permissionRights:[…] )` shape is not one the seed applies.
- A split `Jde.AppServer` + `Jde.OpcGateway` pair (`apps/AppServer`, `apps/OpcGateway` - not shipped by this installer) shares
  port 1967 with the hub; the installer stops them and says so.  Deregister them with each exe's `-uninstall`.
- Logs: `C:\ProgramData\Jde-Cpp\OpcHub\Opc.Hub.log` and `OpcServer\Opc.Server.log` are the text logs (a start rolls the
  previous run aside as `Opc.Hub.1.log` … `.3.log` rather than erasing it, and a file rolls at 10 MB - `logFile` in the
  args; the tags' levels and `flushOn` under each config's `logging.spd` - the shipped `flushOn: "Trace"` writes every line
  through at once), and `OpcHub\opc-hub\log.binpb` / `OpcServer\opc-server\log.binpb` the binary ones the Web UI's Logs
  page reads.  A service has no console, so a failure before logging is up shows only in the Windows event log.  A
  running service's log lists as 0 bytes in `dir` and Explorer until it is opened - read it, do not trust the listing.
- The OpcServer waits for the hub: started before the hub listens, or before a first start of the hub has written the
  certificate it anchors, it logs a warning and retries every 5 seconds (`/server/reconnectWait`) until the hub answers.
- `JDE_PASSCODE` (the private keys' passphrase, `$(JDE_PASSCODE)` in the configs) is unset for a service under Local Service, so
  the keys are written in the clear - the documented behaviour of an empty passcode.  Set it as a system environment variable
  and restart the services:  a key already written in the clear is encrypted with it at that start - the same key, so no certificate changes.  A key written under one passcode does not open under another.
- The hub's web certificate (`OpcHub.pem`, self-signed, issued on the first start) names `localhost`, this machine's name and
  `127.0.0.1`; `hostNames` in `config\apps\OpcHub\config\args\install\args.libsonnet` adds the others a browser or a split
  OpcServer reaches the hub by (a fully qualified name, an alias), and a change re-issues the certificate on the same key at the
  next start.  Trust is a separate matter: a self-signed certificate is untrusted until it is imported where it should be
  trusted (`certutil -addstore Root C:\ProgramData\Jde-Cpp\OpcHub\ssl\certs\OpcHub.pem`, as an administrator), or replaced by
  one a CA your browsers trust issued - `certificate:{ managed:false, path:… }` and `privateKey:{ path:…, passcode:… }` in the
  same args file use that pair as found and never issue or replace it (both files must exist; the public key file the hub's
  identity reads is derived from the private key).  The Web UI uses plain HTTP on 1967 by ruling and needs none of this.
- Connecting the hub to another OPC UA server (`/apps` > the OpcHub card > Connections > Add): set the connection's Certificate URI to that server's
  application URI and the gateway opens a Sign & Encrypt session - Aes256_Sha256_RsaPss, Aes128_Sha256_RsaOaep or
  Basic256Sha256, the strongest the server shares - with a certificate it issues for the connection
  (`C:\ProgramData\Jde-Cpp\OpcHub\ssl\certs\OpcHub.<slug>.pem` - labelled with the hub's own application URI, `urn:<machine>:Jde-Cpp:OpcHub`, which is how it introduces
  itself to every server; the Certificate URI is the server's and only selects its endpoints); an empty URI is an unsecured session - the data in the
  clear, the credential still encrypted to the server's certificate - for a server that publishes an unsecured endpoint at
  that URL.  Trust is two-way and manual: copy the
  server's certificate into `C:\ProgramData\Jde-Cpp\OpcHub\ssl\servers` (created on the first start; `gateway.trustedCertDirs`
  in `config\apps\OpcGateway\config\Opc.Gateway.jsonnet` - the servers the gateway talks to, a list apart from the
  certificates that may log in to the hub), and trust the hub's certificate above in the server's own trust list.  The Web UI's Gateways help topic (`?`) has the details.
- `release.mutation` seeds the access schema without the Google provider rows `access.mutation` (the dev seed) carries; the
  OPC UA Server component adds them (`access_google.mutation` - First login, above).  The roles grant on `opc.install`, the
  schema the installed OpcServer registers its nodes under (`Opc.Server.Install.jsonnet`'s `resource: "install"`).
- SQL Server instead of sqlite, by hand: `apps/OpcHub/config/args/install-sqlServer/args.libsonnet` is the equivalent profile.
  Copy it to `config\apps\OpcHub\config\args\install-sqlServer\`, put `Jde.DB.Odbc.dll` (from the build's `bin\`) beside the
  exe, create a 64-bit System DSN `jde` ("ODBC Driver 17 for SQL Server", `Trusted_Connection=Yes`) with a database `jde` in
  which `NT AUTHORITY\LOCAL SERVICE` is `db_owner` (a SQL Server on another machine sees Local Service as ANONYMOUS LOGON: run the service as a domain account or `NT AUTHORITY\NetworkService` there - `sc config Jde.OpcHub obj= "NT AUTHORITY\NetworkService"` - and grant that account Modify on `C:\ProgramData\Jde-Cpp\OpcHub`), copy the `sql\sqlServer\*.sql` scripts of `libs/access`, `apps/AppServer` and
  `apps/OpcGateway` into the product's `sql-sqlServer\` (the profile's `scriptPaths` - not `sql\`, which is the installer's: it recreates it with the sqlite scripts on every reinstall, and keeps the seeds there, which this profile still reads), and re-register the service with `-include=args/install-sqlServer`.  A reinstall re-registers `Jde.OpcHub` with `-include=args/install` - sqlite again - so redo that one step after it; the profile, the dll, the DSN and `sql-sqlServer\` survive.

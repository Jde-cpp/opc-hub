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
| the Angular site | `web\opc\my-workspace\dist\my-workspace\browser` - `web/opc/scripts/setup.sh` runs `ng build` (`-WebDist`, or `-SkipWeb`) |
| [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset) | `$env:UA_NODE_SETS` (`-UaNodeSets`) - DI/IA nodesets for the OpcServer |
| `vc_redist.x64.exe` | the VS 2026 install's `VC\Redist\MSVC\v145\` (`-VcRedist`), or https://aka.ms/vs/18/release/vc_redist.x64.exe - 14.50 or later, the installer's gate: the exes are built with the 14.51 toolset and Microsoft's rule is a redistributable at least as new as the toolset (the VS 2022 line's 14.44 happens to export every symbol they import, checked 09-12, but only by luck); bundled for the all-users mode, skipped with a warning if missing |

```powershell
.\build-setup.ps1                              # -> <BuildDir>\setup\OpcHubSetup-<git describe>.exe
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
| rights | administrator (UAC prompt) | none - a standard user never sees a prompt; an administrator sees one and may still pick this mode |
| program dir | `C:\Program Files\Jde-Cpp` | `%LOCALAPPDATA%\Programs\Jde-Cpp` |
| how the products run | Windows services `Jde.OpcHub`, `Jde.OpcServer` (auto start; `net start`/`net stop`) | Start Menu folder `Jde-Cpp`: a shortcut per product, each a console window (`-c`); optional "Start at logon" component (HKCU Run) |
| VC++ v14 x64 runtime, 14.50 or later | installed, or upgraded when older | must be present already (installing it needs administrator rights) |
| Add/Remove Programs | HKLM | HKCU (`Jde OpcHub (current user)`) |
| data | `C:\ProgramData\Jde-Cpp\<Product>` in both modes - the apps hardcode it (`Process::ProgramDataFolder()`, `libs/db/config/paths-common.libsonnet`).  A standard user can create the tree and owns it; one created by an all-users install is read-only to them, so the installer refuses the current-user mode in that case. | |

Silent: `OpcHubSetup-<v>.exe /S /AllUsers` or `/CurrentUser`, `/OpcServer` to add the OPC UA Server component (there is no
components page to pick it on), `/D=<dir>` for the program dir.

## Components

| component | section | ships |
|---|---|---|
| OPC Hub (`Jde.OpcHub`) | required | `Jde.Opc.Hub.exe` - the AppServer and the OpcGateway in one process, port 1967 |
| OPC UA Server (`Jde.OpcServer`) | optional, off | `Jde.Opc.Server.exe` - opc.tcp 4840, http 1970, DI/IA nodesets + the pumps demo address space; logs in to the hub with its certificate.  With it, the hub's seeds for it - the server as the default connection, its provider row, the Web UI's Google provider (First login, below) |
| Web UI | optional, on | the Angular site under `<program dir>\Web`, served by the hub itself at `http://<host>:1967/` (`Opc.Hub.jsonnet` `http.site`, `$(ExeDir)/../web` from the install args - the page and its api on one origin, no IIS). `web.config` is included for anyone who prefers the site behind IIS (`apps/OpcGateway/README.md`). The page reaches the hub by the host it was browsed from |
| Start at logon | current-user only | HKCU Run entries for the selected products |

## Installed layout

```
<program dir>                                            C:\Program Files\Jde-Cpp  |  %LOCALAPPDATA%\Programs\Jde-Cpp
  OpcHub\     Jde.Opc.Hub.exe Jde.dll Jde.DB.dll fmt.dll z.dll libcrypto-3-x64.dll libssl-3-x64.dll
              Jde.DB.Sqlite.dll sqlite3.dll Jde.DB.Sqlite.AppServer.dll Jde.DB.Sqlite.OpcGateway.dll
  OpcServer\  Jde.Opc.Server.exe + the same + libxml2.dll, Jde.DB.Sqlite.dll sqlite3.dll
  Web\        the Angular site, served by the hub at http://<host>:1967/ (+ web.config for IIS, optional)
  Uninstall.exe
C:\ProgramData\Jde-Cpp
  config\                                                settings mirror - repo layout, so the configs' relative imports keep working
    apps\OpcHub\config\Opc.Hub.jsonnet                   (imports ../../AppServer/config/App.Server.jsonnet, ../../OpcGateway/config/Opc.Gateway.jsonnet)
    apps\OpcHub\config\args\install\args.libsonnet       sqlite; the driver/proc modules by $(ExeDir), the data by $(ProgramData)
    apps\AppServer\config\App.Server.jsonnet
    apps\OpcGateway\config\Opc.Gateway.jsonnet
    apps\OpcGateway\config\introspection\*.jsonnet
    apps\OpcServer\config\Opc.Server.jsonnet + Opc.Server.Install.jsonnet (the overlay the service loads)
    apps\OpcServer\config\args\install\args.libsonnet
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

On a fresh install the login is **Google**, and it comes with the **OPC UA Server** component
([`reviews/install-issues.md`](../../../../reviews/install-issues.md) #1 - by ruling no username or password is seeded, and a
hub installed without the component has no login path): the component seeds the Google provider (`access_google.mutation`)
and `Jde.OpcServer` as the hub's default server connection (`gateway_opcServer.mutation`: slug `OpcServer`,
`opc.tcp://127.0.0.1:4840`; `access_opcServer.mutation`: its provider row).  The button works only from an origin registered
under the OAuth client id the hub serves (`GET /GoogleAuthClientId`).  The default is the project's own client id, so a
browser on the hub machine at `http://localhost:1967` works once that origin is registered on it; any other host needs its
own (Google Cloud console > APIs & Services > Credentials > OAuth client ID, Web application, authorized JavaScript origin
`http://<host>:1967`), put in `config\apps\OpcHub\config\args\install\args.libsonnet` (`googleAuthClientId`), then
`Jde.OpcHub` restarted.  The first grant is manual by ruling (the seeded roles); every resource ships unenforced, so the first
user can make it.

The page's username/password form is the OPC server's login, and the bundled server offers the username token only when
its settings list users (`/opc/users: [{name, password}]` under `config\apps\OpcServer\config\` - an opt-in, nothing
shipped sets it); the form then logs in against the default connection, so the username needs no `DOMAIN\`.

## Uninstall

Add/Remove Programs (or the Start Menu shortcut in a current-user install) stops and deregisters the services (or ends the
console windows and removes the shortcuts/Run entries), removes the program dir, the `config\` mirror and the meta/sql/
nodesets the installer put in the product dirs.  Left in place, deliberately: `OpcHub.db`, `OpcServer.db`, `ssl\`
(certificates and keys - the OPC servers trust them) and the logs.  Delete `C:\ProgramData\Jde-Cpp` by hand for a clean slate.

## Notes

- Reinstalling over an existing install is fine: the services are deregistered and re-registered, the `.db` is kept, the
  installer-owned `sql\` and `nodesets\` are recreated (the settings under `config\` are overwritten - keep a copy of edits).
- Roles are seeded by a second pass: `<schema>.roles` files under `dataPaths` are upserted after the access server is
  configured (`createRole`/`addRole` run through its mutations, which the `.mutation` pass runs too early for).
  `release.roles` ships Viewer, System Administrator, Owner, Engineer, Operator and Maintenance Technician; `addRole` names
  roles by `slug`, and a rerun on a later `-sync` start changes nothing.  `apps/OpcGateway/config/access-opcGateway.mutation`
  (the gateway's group/role) is still not seeded: its `createRole( permissionRights:[…] )` shape is not one the seed applies.
- A split `Jde.AppServer` + `Jde.OpcGateway` pair (`apps/AppServer`, `apps/OpcGateway` - not shipped by this installer) shares
  port 1967 with the hub; the installer stops them and says so.  Deregister them with each exe's `-uninstall`.
- `JDE_PASSCODE` (the private keys' passphrase, `$(JDE_PASSCODE)` in the configs) is unset for a service under LocalSystem, so
  the keys are written in the clear - the documented behaviour of an empty passcode.  Set it as a system environment variable
  before the first start to change that.
- The hub's web certificate (`OpcHub.pem`, self-signed, issued on the first start) names `localhost`, this machine's name and
  `127.0.0.1`; `hostNames` in `config\apps\OpcHub\config\args\install\args.libsonnet` adds the others a browser or a split
  OpcServer reaches the hub by (a fully qualified name, an alias), and a change re-issues the certificate on the same key at the
  next start.  Trust is a separate matter: a self-signed certificate is untrusted until it is imported where it should be
  trusted (`certutil -addstore Root C:\ProgramData\Jde-Cpp\OpcHub\ssl\certs\OpcHub.pem`, as an administrator), or replaced by
  one a CA your browsers trust issued - `certificate:{ managed:false, path:… }` and `privateKey:{ path:…, passcode:… }` in the
  same args file use that pair as found and never issue or replace it (both files must exist; the public key file the hub's
  identity reads is derived from the private key).  The Web UI uses plain HTTP on 1967 by ruling and needs none of this.
- `release.mutation` seeds the access schema without the Google provider rows `access.mutation` (the dev seed) carries; the
  OPC UA Server component adds them (`access_google.mutation` - First login, above).  The roles grant on `opc.install`, the
  schema the installed OpcServer registers its nodes under (`Opc.Server.Install.jsonnet`'s `resource: "install"`).
- SQL Server instead of sqlite, by hand: `apps/OpcHub/config/args/install-sqlServer/args.libsonnet` is the equivalent profile.
  Copy it to `config\apps\OpcHub\config\args\install-sqlServer\`, put `Jde.DB.Odbc.dll` (from the build's `bin\`) beside the
  exe, create a 64-bit System DSN `jde` ("ODBC Driver 17 for SQL Server", `Trusted_Connection=Yes`) with a database `jde` in
  which `NT AUTHORITY\System` is `db_owner`, copy the `sql\sqlServer\*.sql` scripts of `libs/access`, `apps/AppServer` and
  `apps/OpcGateway` into the product's `sql\`, and re-register the service with `-include=args/install-sqlServer`.

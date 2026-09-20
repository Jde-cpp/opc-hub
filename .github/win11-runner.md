# Self-hosted Windows 11 CI runner

Setup notes for the **Win11** workflow
([`.github/workflows/win11-ci.yml`](workflows/win11-ci.yml)). Unlike the Linux runner
([`.github/docker/`](docker/README.md)), which is a containerised, ephemeral, auto-registering
runner, the Windows runner is a **bare runner on a real workstation** using the developer toolchain
already installed there. Because it is not sandboxed, the workflow runs **only on manual request**
(`workflow_dispatch`) — never on push/PR.

## Register the runner

The runner lives in `C:\actions-runner` on the workstation and is registered as `jde-ci-win11`.
If the folder is missing, download the runner from repo **Settings → Actions → Runners → New
self-hosted runner (Windows x64)**. Then, in an **elevated** PowerShell in the runner folder:

```powershell
# A registration token expires after an hour; mint one here or copy it from the
# "New self-hosted runner" page.
$token = gh api -X POST repos/Jde-cpp/opc-hub/actions/runners/registration-token --jq .token

# --runasservice installs and starts the Windows service in the same step. There is no
# svc.cmd on Windows (svc.sh is the Linux/macOS script). The service must run as the dev
# account - see "Service account requirements" below.
.\config.cmd --unattended --url https://github.com/Jde-cpp/opc-hub --token $token `
  --name jde-ci-win11 --labels win11-clang --replace `
  --runasservice --windowslogonaccount jde-cpp\duffyj --windowslogonpassword '<password>'
```

The password goes into PSReadLine's history file; delete that line afterwards
(`(Get-PSReadLineOption).HistorySavePath`). The service is
`actions.runner.Jde-cpp-opc-hub.jde-ci-win11`, start type Automatic. Confirm the runner shows
**Idle** under Settings → Actions → Runners, or from a shell:

```powershell
gh api 'repos/{owner}/{repo}/actions/runners' --jq '.runners[] | "\(.name) \(.status)"'
# a bare `gh api actions/runners` is a 404 - gh does not infer the repo from the cwd
```

The `win11-clang` label is what `runs-on: [self-hosted, win11-clang]` targets; GitHub also
auto-adds `self-hosted`, `Windows`, and `X64`. Keep the label distinct from the Linux runner's
`clang23` so a job never lands on the wrong OS.

**It has to be a service.** GitHub deletes a self-hosted runner that stays disconnected for
14 days. The first registration (2026-07-24) was only ever started from `run.cmd`; the listener
died with its console on 07-25, the runner was gone by August, and every dispatch would have
queued forever - the repo listed only the Linux runner. Re-registered as a service 2026-09-07.

**Re-registering after a prune:** `config.cmd` refuses to configure while `.runner` exists, and
`config.cmd remove` wants a removal token the server can no longer match to a runner, so clear
the local config first, then run the registration above:

```powershell
.\config.cmd remove --local   # drops .runner, .credentials, .credentials_rsaparams
```

**Public-repo safety:** self-hosted + public repo means untrusted code must never execute here.
`workflow_dispatch` can only be triggered by users with write access, so — unlike the Linux
workflow's `push`/`pull_request` triggers — no fork-PR `if` fence is required. Also keep repo
**Settings → Actions → General → Fork pull request workflows** at "require approval for all outside
collaborators".

## Service account requirements

Run the service as `duffyj`, the interactive dev account, for two reasons:

- **PATH.** The `win-clang-debug-jde` preset resolves bare `clang++` / `ld.lld` from `PATH`, and
  on this box `C:\Program Files\LLVM\bin` is on the *user* PATH only (CMake and Ninja are on the
  machine PATH). A SYSTEM or Network Service account fails Configure with the compiler not found.
- **Symlinks.** [`build/functions.cmake`](../build/functions.cmake) creates a configure-time
  symlink for the static config files (`file(CREATE_LINK … SYMBOLIC)`); the per-build
  `create_symlink` steps and the generated-header links are gone. Developer Mode is enabled on the
  workstation (Settings → For developers), which lets any non-elevated process create symlinks;
  without it the service account would need `SeCreateSymbolicLinkPrivilege`.

## Toolchain (on the service PATH)

The `win-clang-debug-jde` preset uses bare compiler names resolved from `PATH`, so the runner
**service** environment (not just your interactive shell) must expose:

- LLVM **clang++** and **ld.lld** 23 — 23.1.1, installed 2026-09-19 from the official
  `LLVM-23.1.1-win64.msi` into `C:\Program Files\LLVM` (the same path 22 used, so the user
  PATH entry above needed no change). 23.x ships an MSI rather than the NSIS `.exe` of
  earlier releases, and the MSI has no `Environment` table — it never touches PATH itself.
  The MSI also omits a few tools the NSIS installer carried, notably `llvm-dwarfdump`;
  pull those from the `clang+llvm-<ver>-x86_64-pc-windows-msvc.tar.xz` if needed.
- **CMake** ≥ 4.2.3
- **Ninja**
- the Windows SDK providing **`dbgeng.lib`** — the preset links `-ldbgeng` and defines
  `BOOST_STACKTRACE_USE_WINDBG` for Windows stacktraces.

## Prebuilt dependency trees

The `-jde` build does **not** build third-party dependencies; they must already be present
(built with the `win-clang-*-repos` presets):

| Path | Purpose |
|------|---------|
| `%REPO_DIR%\install\clang++\Debug` | fmt / zlib / libxml2 / open62541 / protobuf / abseil / gtest / spdlog / jsonnet / ryml / NodesetLoader — `CMAKE_PREFIX_PATH`. fmt/zlib/libxml2 ship DLLs that CMake POST_BUILD-copies next to each test exe. |
| `%REPO_DIR%\boostorg\boost_1_91_0` | Boost **source** — on Windows `functions.cmake` compiles `boost_json` from source (no `find_package(Boost)`). |
| `%VCPKG_ROOT%\installed` | vcpkg deps for triplet `x64-windows` (OpenSSL, sqlite3), **dynamically** linked - `libcrypto-3-x64.dll`, `libssl-3-x64.dll`, `sqlite3.dll` ship beside the binaries.  Was `x64-windows-static-md`: a static OpenSSL was linked into every module that touched crypto (4.5 MB of `Jde.DB.MySql.dll`, 3.9 MB of `Jde.dll`), and two copies meant two error stacks and two RNGs in one process. |
| `%UA_NODE_SETS%` | Clone of [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset) — the OpcServer UALoadTests read it. |

These paths are set explicitly in the workflow `env:` block (the service account may not inherit the
user-level env vars): `REPO_DIR=C:/Users/duffyj/source/repos/libs`,
`VCPKG_ROOT=C:/Users/duffyj/source/repos/libs/vcpkg`,
`UA_NODE_SETS=C:/Users/duffyj/source/repos/libs/UA-Nodeset`.

## Build directory

The workflow sets a **CI-only** `JDE_BUILD_DIR=C:/Users/duffyj/source/build/ci`, kept separate from
the local dev build (`x:\build\clang++\opc-hub\debug`) so the two never fight over one build tree. The
preset appends `\clang++\${sourceDirName}\debug` and the runner always checks out to
`…\_work\opc-hub\opc-hub`, so the actual build dir is
`C:\Users\duffyj\source\build\ci\clang++\opc-hub\debug`.

**Enable long paths** — deep object paths under the CI build root can approach the 260-char limit
(object files embed the full source path):

```powershell
git config --system core.longpaths true
# and set HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 1
```

Both are precautionary: the 2026-09-07 clean build (run #3) passed with neither set. A clean
Debug build of the tree is ~21 GB and took 12.4 min (Configure 15 s, ctest 14/14 in 182 s), so
`C:` needs that much free before a run.

## Notes / trade-offs

- **Incremental builds:** the CI build dir is disk-backed and persistent (not a wiped ramdisk like
  Linux `/mnt/ram`), so Ninja reuses objects across runs — faster, but not a clean build each time.
  Delete `C:\Users\duffyj\source\build\ci` to force a clean rebuild.
- **Antivirus:** Defender scanning thousands of `.obj`/PDB files slows builds and can intermittently
  lock files → flaky link failures. Exclude the CI build root (and LLVM/vcpkg) from real-time scanning.
- **Tests need no DB server:** `addJdeTest` wires the DB-backed suites (access / OpcGateway /
  OpcServer) to in-memory sqlite on every platform, and all runtime DLLs are co-located in
  `<build>\bin`, so no `PATH` tweaks are needed — provided a full `cmake --build` runs before `ctest`.
- **Generated-header ordering (the serial-fallback canary):** protobuf codegen is out-of-source
  since 2026-08-14 - every `*.pb.h` lands under `<buildDir>/include` - but a target that includes
  one must still *depend* on the target whose protoc step writes it (`add_dependencies`, or a
  link edge), or on a clean parallel build its compiles can run before protoc. Win11 run #3
  (2026-09-07, the first clean build after the move) hit exactly that: seven `Jde.Web.Server`
  objects failed on `jde/app/proto/Log.pb.h` not found (reached via `IApp.h` → `ProtoLog.h`), and
  the workflow's serial `-j 1` fallback finished the build. The missing edge,
  `add_dependencies( Jde.Web.Server Jde.App.Shared )` in
  [`libs/web/server/CMakeLists.txt`](../libs/web/server/CMakeLists.txt), was added the same day.
  The fallback stays as a canary: if its warning fires again, a new consumer is missing its edge.
  The older in-source share-lock race (`"…: Invalid argument"`) is gone with the move.

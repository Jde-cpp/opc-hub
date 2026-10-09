---
name: cpp-build-win
description: How to configure and build the Jde C++ tree on Windows — LLVM clang/lld 23 on the VS 2026 runtime with Ninja, vcpkg (openssl, sqlite3) and Boost from source, the win-clang presets and the build dirs they set themselves, the Git Bash helpers in buildFunctions-win.sh (reconfig/compile/buildProject/clean) and their raw cmake equivalents, the bin\ output layout, the Windows-only targets, the third-party tree, the release build the installer packs, and the vcpkg-baseline, protobuf-ordering, PATH, symlink and Defender gotchas. Use when configuring, building, rebuilding, or cleaning C++ in this repo on Windows, or when a Windows configure/build fails. (Linux: the cpp-build-linux skill.)
---

# Building (C++) on Windows

The build is C++26 with **LLVM clang/lld 23** (`clang++` on the MSVC ABI, not clang-cl) against the **VS 2026** toolset's
runtime (`/MD`, `/MDd` in Debug), the **Ninja** generator, CMake **4.2.3 or later**. openssl and sqlite3 come from
**vcpkg** (`vcpkg.json`), Boost from a **source tree**, everything else from the third-party superbuild. Two workflows
check it: `win11-ci.yml` (the self-hosted workstation: Debug + ctest) and `win2025-build.yml` (the hosted image:
Release + installer). Their long-form notes are `.github/win11-runner.md` and `.github/win2025-hosted.md`.

## Prerequisites

| what | notes |
|------|-------|
| LLVM 23 (23.1.1) | `LLVM-23.1.1-win64.msi` → `C:\Program Files\LLVM`. **The MSI never touches PATH**: add `…\LLVM\bin` yourself, since the presets name bare `clang`/`clang++` and link with `-fuse-ld=lld`. It lacks a few tools the `.tar.xz` has (`llvm-dwarfdump`) |
| VS 2026 + Windows SDK | the MSVC runtime and headers clang targets, and `dbgeng.lib` (`-ldbgeng`, for `BOOST_STACKTRACE_USE_WINDBG`) |
| CMake ≥ 4.2.3, Ninja | on PATH |
| vcpkg at `$VCPKG_ROOT` | a git clone at or past `vcpkg.json`'s `builtin-baseline`; every `-jde` and `-repos` configure runs the manifest install |
| Boost 1.91.0 **source** | unpacked at `$REPO_DIR/boostorg/boost_1_91_0`, the path `boost()` in `build/functions.cmake` hard-codes. Linux is on 1.92, built by `boost-sqlite.sh` |
| Developer Mode | `linkConfigFile()` (`functions.cmake`) makes configure-time symlinks with `file(CREATE_LINK … SYMBOLIC)`; without Developer Mode the account needs `SeCreateSymbolicLinkPrivilege` |
| long paths | `git config --system core.longpaths true` and `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled=1`. Object paths embed the full source path; precautionary so far |
| Git Bash + `jq` | only for the `buildFunctions-win.sh` helpers |

## Environment variables

| variable | role (dev-box value) |
|----------|---------------------|
| `$REPO_DIR` | third-party root (`C:/Users/duffyj/source/repos/libs`), **not** the checkout. Deps install under `$REPO_DIR/install/clang++/<Debug\|Release>/<dep>`; the Boost source sits in `boostorg/` |
| `$VCPKG_ROOT` | the vcpkg clone (`$REPO_DIR/vcpkg`); the presets take the toolchain file and `installed/` from it |
| `$JDE_DIR` / `$JDE_BASH` | this checkout's source root (`C:\Users\duffyj\source\repos\jde\opc-hub`); the helpers source `$JDE_BASH/build/common.sh` |
| `$JDE_BUILD_DIR` | build-output parent (`x:\build`), read by every preset's `binaryDir` |
| `$JDE_RBUILD_DIR` | optional root for release trees (`build-setup.ps1` falls back to `R:\`); the VS Code `jde.repoBuildRelDir` command reads it |
| `$JDE_COMPILER` | `clang++`. The presets hard-code that path segment, so the extension's `jde.repoBuildDir` only matches them when it is `clang++` |
| `$UA_NODE_SETS` | the OPCFoundation/UA-Nodeset clone (`$REPO_DIR/UA-Nodeset`), for the nodeset-load tests and the installer |
| `$CXX` | need not be exported: the hidden `clang` preset sets `CXX=clang++`, so `installRoot` is `$REPO_DIR/install/clang++` |

## Presets and build dirs

`CMakePresets.json` includes `CMakePresets.${hostSystemName}.json`, so only `CMakePresets.Windows.json` (+ `common`) is
visible here. Configure presets only — no build or test presets. **Unlike Linux, every Windows preset sets `binaryDir`**,
so `cmake --preset <p>` from the checkout root configures on its own; `-B` is optional and overrides it.

| preset | builds | binaryDir |
|--------|--------|-----------|
| `win-clang-debug-jde` | this repo, Debug: `-gdwarf-4` (for lldb), no sanitizers, `compile_commands.json` | `$JDE_BUILD_DIR/clang++/<checkout>/debug` → `x:\build\clang++\opc-hub\debug` |
| `win-clang-release-jde` | this repo, Release: CodeView embedded in the objects, `/OPT:REF /OPT:ICF`. What the installer packs | `$JDE_BUILD_DIR/clang++-jde/release`, **with no checkout segment** |
| `win-clang-debug-repos` | the third-party tree → `$REPO_DIR/install/clang++/Debug` | `$JDE_BUILD_DIR/clang++-repos/debug` |
| `win-clang-release-repos` | same → `install/clang++/Release` | `$JDE_BUILD_DIR/clang++-repos/release` |

There is no relWithDebInfo and no g++ preset on Windows. `win-clang` sets Ninja; the vcpkg toolchain file with
triplet `x64-windows` (dynamic: `libcrypto-3-x64.dll`, `libssl-3-x64.dll` and `sqlite3.dll` ship beside the binaries)
and `VCPKG_INSTALLED_DIR=$VCPKG_ROOT/installed`, shared by every checkout; `-DWIN32_LEAN_AND_MEAN` plus the `cpuFlags`
(`-march=x86-64-v3 -mpclmul -maes`); and `-fuse-ld=lld -Wl,/ignore:longsections -ldbgeng` for every link.
`CMAKE_CXX_FLAGS_DEBUG` is `$env{cxxFlagsDebug}`, empty unless exported. `compileOptions()` makes warnings errors here too.

- **The release tree.** The preset's `clang++-jde/release` is shared by every checkout; CI builds there
  (`C:/jde/build/clang++-jde/release`), as does `apps/OpcGateway/soak/soak.sh`. The local release tree is
  `$JDE_RBUILD_DIR\clang++\<checkout>\release`, `build-setup.ps1`'s default `-BuildDir`, so pass `-B` for it.
- **Switching one dir between `-repos` and `-jde`** keeps the cached `jde_REPOS`/`jde_TESTS`/`jde_APPS`: the `-jde`
  presets set none of them and rely on `option()` defaults that only apply to a fresh cache. The separate binaryDirs
  avoid this unless `-B` points both at one dir; then delete `CMakeCache.txt`, or pass `-Djde_REPOS=OFF -Djde_TESTS=ON -Djde_APPS=ON`.

## Configure and build — raw cmake

From the checkout root, in PowerShell:

```powershell
$B = "$env:JDE_BUILD_DIR\clang++\opc-hub\debug"
cmake --preset win-clang-debug-jde -Wno-dev          # into $B; keeps CMakeCache.txt, so incremental
cmake --build $B -j $env:NUMBER_OF_PROCESSORS
cmake --build $B --target Jde.DB Jde.DB.Tests
ctest --test-dir $B --output-on-failure --no-tests=error   # serial; the cpp-tests skill has the rest

# release, for the installer (apps/OpcHub/setup/README.md)
$R = "$env:JDE_RBUILD_DIR\clang++\opc-hub\release"
cmake -B $R --preset win-clang-release-jde -Wno-dev
cmake --build $R --target Jde.Opc.Hub Jde.Opc.Server Jde.DB.Sqlite Jde.DB.Sqlite.AppServer Jde.DB.Sqlite.OpcGateway Jde.DB.Odbc Jde.DB.MySql Jde.CheckCpu
.\apps\OpcHub\setup\build-setup.ps1                 # -> $R\setup\OpcHubSetup-<version>.exe
```

`-Wno-dev` is what `reconfig` passes; CI's `cmake --preset` calls omit it.

## The helpers — `build/buildFunctions-win.sh` (Git Bash)

It sources `buildFunctions.sh`, so `reconfig` and `build` are the shared ones; `compile`, `buildProject` and `clean`
are redefined for Ninja. Every helper takes the **full** build dir. Write it with forward slashes, as
`jde.repoBuildDir` hands it to the VS Code tasks; `compile` and `buildProject` convert `C:\…` paths themselves (`toBashDir`).

```bash
source $JDE_BASH/build/buildFunctions-win.sh
B=x:/build/clang++/opc-hub/debug

reconfig $B $JDE_DIR win-clang-debug-jde              # deletes CMakeCache.txt, makes runtime/logs, cmake -B, copies compile_commands.json to the checkout
build $B $JDE_DIR Jde.Access.Tests                    # cmake --build . -j [--target], teed to <target>.output
compile $B $JDE_DIR/libs/fwk/src/io/json.cpp          # one TU
buildProject $B $JDE_DIR/libs/web/server/Server.cpp   # the file's directory target
clean $B
```

| helper | Windows behaviour |
|--------|-------------------|
| `compile <buildDir> <file>` | Ninja has no per-directory Makefiles, so it looks the `.obj` up in `compile_commands.json` (via `jq`) and runs `ninja -C <buildDir> <obj>`. A file with no entry fails "no compile command" — a header, or a new source the glob has not picked up yet |
| `buildProject <buildDir> <file>` | `ninja <dir>/all` for the nearest dir with its own CMakeLists. Two arguments, where Linux takes `<buildDir> <workspaceFolder> <relativeFile>` |
| `clean <buildDir>` | `--target clean`, then deletes **every** `*.obj *.lib *.exe *.pdb *.pcm *.json *.xml *.yml *.yaml …` under the build dir, `compile_commands.json` and CMake's file-api replies included: reconfigure afterwards |

The `msbuild.exe` branches are legacy; no preset generates Visual Studio projects.

## Output layout

Not Linux's mirrored `lib/`/`exe/` tree: `functions.cmake` sends every runtime, library, archive and pdb output to
`<buildDir>\bin` (`Jde.dll`, `Jde.lib`, `Jde.DB.Sqlite.dll`, `sqlite3.dll`, most test exes). The apps and their test
suites, plus `Jde.Fwk.Tests`, get `bin\<target>\` instead (`bin\Jde.Opc.Hub\Jde.Opc.Hub.exe`, `bin\Jde.Opc.Server\…`),
and `copyCommonDlls()` stages `Jde.dll`, `Jde.DB.dll` and the fmt/zlib/abseil DLLs beside them. After a full build
ctest needs no PATH changes.

A direct run (debugger, by hand) is as on Linux: cwd `<buildDir>\runtime`, `REPO_SOURCE_DIR=$JDE_DIR`,
`REPO_BUILD_DIR=` the build dir's **parent**. The `-tests` default import dir is `config/args/sqlServer` on Windows;
use `-include=args/sqlite -arg path=<file>` without SQL Server.

## Targets

The same names as Linux (the cpp-build-linux skill has the table), plus three Windows-only ones:

- `Jde.DB.Odbc`, `Jde.DB.Odbc.Tests`: the odbc driver is Windows-only (root `CMakeLists.txt`).
- `Jde.CheckCpu` (`apps/OpcHub/CMakeLists.txt`): Setup runs it before installing anything, the VC++ runtime included,
  so it links no Jde library and uses the static CRT. It must import `KERNEL32.dll` alone, which `win2025-build.yml`
  checks with `llvm-readobj --coff-imports`.

`cmake --build $B --target help` lists them all.

## The third-party tree

```powershell
cmake --preset win-clang-debug-repos -Wno-dev
cmake --build "$env:JDE_BUILD_DIR\clang++-repos\debug" -j $env:NUMBER_OF_PROCESSORS
```

- The superbuild (`build/CMakeLists.txt`) is Linux's, plus **zlib and libxml2**, which it builds only on Windows
  (Linux takes them from apt).
- The configure runs the vcpkg manifest install first (openssl, sqlite3 → `$VCPKG_ROOT/installed`); that is where
  open62541 finds OpenSSL.
- Boost is not built at all: `boost()` compiles Boost.JSON into `Jde.dll`, which exports it (everyone else sees
  `BOOST_JSON_DYN_LINK`), and `charconv`, which Boost.MySQL needs, into a static lib, both from the source tree.
- CI splits it: `win2025-deps.yml` (dispatch on `main`) builds the Release deps and caches them, keyed on the presets,
  `build/CMakeLists.txt`, `functions.cmake` and `vcpkg.json`; `win2025-build.yml` fails on a cache miss rather than
  building deps. A dep or preset bump, or 7 idle days (cache eviction), means re-running Win2025 Deps first.

## Gotchas

- **`no version database entry for openssl at …`** at configure: the vcpkg clone is behind `vcpkg.json`'s baseline.
  vcpkg reads the baseline out of git but the versions out of the working tree, so a fetch alone is not enough:
  `git -C $env:VCPKG_ROOT pull`, then `& $env:VCPKG_ROOT\bootstrap-vcpkg.bat -disableMetrics`.
- **A `*.pb.h` not found on a clean parallel build** (`jde/app/proto/Log.pb.h`, via `IApp.h` → `ProtoLog.h`): a
  target includes a generated header without depending on the target whose protoc step writes it. Add
  `add_dependencies( <consumer> <producer> )`, as `libs/web/server` does for `Jde.App.Shared`. Both CI workflows
  retry a failed parallel build with `-j 1`; that warning firing is the signal, not a fix.
- **Compiler not found** when configuring from a service or another account: LLVM is on the dev account's *user*
  PATH only, which is why the self-hosted runner runs as that account.
- **Symlink errors at configure** (`CREATE_LINK`): Developer Mode is off.
- **Flaky link failures or a slow build**: Defender scanning thousands of `.obj`/`.pdb` files. Exclude the build
  root, LLVM and vcpkg from real-time scanning.
- **Size**: a clean Debug tree is ~21 GB (12 min on the workstation). Build dirs are disk-backed and persist, so
  Ninja stays incremental; delete the dir for a clean build.
- **A new source file** needs a reconfigure: the repo-wide `file(GLOB)`s have no `CONFIGURE_DEPENDS`.

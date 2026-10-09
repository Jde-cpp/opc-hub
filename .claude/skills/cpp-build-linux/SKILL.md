---
name: cpp-build-linux
description: How to configure and build the Jde C++ tree on Linux — the clang 23/libc++ toolchain, the $REPO_DIR vs $JDE_DIR roles and the build-dir layout, the buildFunctions.sh helpers (reconfig/build/compile/clean) and their raw cmake equivalents, the mandatory -B, the OS-split presets with the -jde/-repos tables, the third-party superbuild, target names, and the stale-PCH and parallelism gotchas. Use when configuring, building, rebuilding, or cleaning C++ in this repo on Linux, or when a configure/build fails. (Windows: the cpp-build-win skill.)
---

# Building (C++) on Linux

The build is C++26 on **clang 23 with libc++ and lld**, CMake **4.2.3 or later**, Ubuntu 24.04 or later. The
unversioned `clang`/`clang++`/`ld.lld` must resolve to the v23 binaries (`update-alternatives`; both 21 and 23 are
installed on this machine) — the presets name the compiler as plain `clang++`. The system packages the presets link
against are `libxml2-dev liburing-dev libssl-dev libzstd-dev zlib1g-dev` plus libbacktrace (`-lbacktrace`, apt from
Ubuntu 25.04) and libjitterentropy (`libjitterentropy3-dev` on 26.04). `.github/docker/Dockerfile` is the authoritative
package list — it builds the two from source for noble.

## Environment variables

| variable | role |
|----------|------|
| `$REPO_DIR` | third-party root (`/home/duffyj/code/libs`), **not** the jde checkout. Deps install under `$REPO_DIR/install/$CXX/<Debug\|RelWithDebInfo>/<dep>`; Boost and sqlite sources sit beside `install/` |
| `$JDE_DIR` / `$JDE_BASH` | this checkout's source root (`/home/duffyj/code/jde/opc-hub`) |
| `$JDE_BUILD_DIR` | build-output parent (`/mnt/ram/linux` — a tmpfs restored from a snapshot at boot, see *Gotchas*) |
| `$JDE_COMPILER` | subdirectory under `$JDE_BUILD_DIR` (`clang++`) |
| `$CXX` | read by the presets' `installRoot` (`$env{REPO_DIR}/install/$env{CXX}`). **Need not be exported for the clang presets** — the hidden `clang` preset sets `CXX=clang++` in its own environment. Export it (`g++-15`) only for the g++ `-repos` presets |
| `$JDE_RBUILD_DIR` | optional: a separate root for release outputs; the VS Code extension falls back to `$JDE_BUILD_DIR` |
| `$UA_NODE_SETS` | the OPCFoundation/UA-Nodeset clone (`$REPO_DIR/UA-Nodeset`); the OpcServer/Gateway/Hub configs and tests read `$(UA_NODE_SETS)` |

## Build-dir layout

| tree | path | contents |
|------|------|----------|
| this repo | `$JDE_BUILD_DIR/$JDE_COMPILER/<checkout-basename>/<debug\|release>` → `/mnt/ram/linux/clang++/opc-hub/debug` | mirrors the source tree with `src`→`lib` and an app's root→`exe`: `libs/fwk/lib/libJde.so`, `apps/OpcHub/exe/Jde.Opc.Hub`, `libs/fwk/tests/Jde.Fwk.Tests`. Also `runtime/` (direct-run cwd, made by `reconfig`), `Testing/` (ctest cwd), `compile_commands.json`, and the `*.output` build logs |
| third-party superbuild | `$REPO_DIR/build/<debug\|relWithDebInfo>` | ExternalProject sources and builds under `repos/<dep>/src/<dep>`; the open62541 tree to grep is `$REPO_DIR/build/debug/repos/open62541/src/open62541` (v1.5.9), **not** `~/code/open62541` (1.4.10) |
| installed deps | `$REPO_DIR/install/clang++/<Debug\|RelWithDebInfo>/<dep>` | one subdir per dep (`absl boost fmt gtest jsonnet NodesetLoader open62541 protobuf ryml spdlog sqlite`). `CMAKE_PREFIX_PATH` is the `<Debug\|RelWithDebInfo>` dir; config-mode `find_package` searches `<prefix>/<name>*/lib/cmake` so the per-dep subdirs resolve on their own (the sqlite driver appends `/sqlite` itself) |

Several checkouts coexist by basename (`opc-hub`, `opc-hub2`), each with its own build dir, all sharing one install tree.

## Presets

`CMakePresets.json` includes `CMakePresets.${hostSystemName}.json`, so on Linux only `CMakePresets.Linux.json` (+ `common`)
is visible. **Configure presets only** — there are no build or test presets (removed 2026-08-14, reviews/todo.md §3);
`cmake --build <dir>` and `cd <dir> && ctest` do the rest, and `--preset` on either of those does not exist.

| preset | builds | into |
|--------|--------|------|
| `linux-clang-debug-jde` | this repo, Debug: ASan + LSan, `-glldb`, `compile_commands.json` | `…/<checkout>/debug` |
| `linux-clang-relWithDebInfo-jde` | this repo, the release build the installers pack | `…/<checkout>/release` |
| `linux-clang-debug-repos` | the third-party tree (`jde_REPOS=ON`, apps/tests off) | `$REPO_DIR/build/debug` → `install/clang++/Debug` |
| `linux-clang-relWithDebInfo-repos` | same, release | `$REPO_DIR/build/relWithDebInfo` → `install/clang++/RelWithDebInfo` |
| `linux-debug-repos`, `linux-relWithDebInfo-repos` | g++ variants of the repos tree only (`export CXX=g++-15`); there is **no g++ `-jde` preset** and that path is unmaintained | `install/$CXX/…` |

The rest (`common repos debug relWithDebInfo clang clang-jde linux linux-debug linux-relWithDebInfo linux-clang
linux-clang-debug linux-clang-relWithDebInfo linux-repos`) are hidden building blocks.

Three things every configure needs, and why:

- **`-B <buildDir>` is mandatory.** No Linux preset sets `binaryDir`, and preset mode ignores the cwd — without `-B`
  cmake configures into the *source* dir.
- **Always a preset, never raw `-D` flags.** The root CMakeLists fails without `MIN_REQ_CMAKE_VERSION`, and
  `build/dependencies.cmake` fails the configure if `CMAKE_CXX_FLAGS` lacks `-march=x86-64-v3 -mpclmul -maes`
  (`cpuFlags`), because abseil/protobuf were built with them and hash differently without.
- **`-Wno-dev`** — what `reconfig` and CI pass.

Other cache knobs: `-Djde_TESTS=OFF` (the release workflow), `-Djde_APPS=OFF`, `-DJDE_TEST_TIMEOUT=<s>` (ctest
per-suite kill, default 300). `compileOptions()` adds `-Wall -Wextra -pedantic -Werror -Wthread-safety` to every
target, so **warnings are errors**; an `OPTIMIZATION_LEVEL` env var (e.g. `O1`) adds `-O<level>` to every target.

## The helpers — `build/buildFunctions.sh`

`source $JDE_DIR/build/buildFunctions.sh`. Function bodies are subshells, so nothing (cd, pipefail, variables) leaks
into the caller; it does **not** source `common-error.sh`, so no ERR trap. Every helper takes the **full** build dir.

| helper | does |
|--------|------|
| `reconfig <buildDir> <sourceDir> <preset>` | `mkdir -p <buildDir>/runtime/logs`, **deletes `CMakeCache.txt`** (a from-scratch configure), `cmake -B -S -Wno-dev --preset`, tees to `<buildDir>/cmake.output`, copies `compile_commands.json` into the source root for clangd |
| `build <buildDir> <sourceDir> [target]` | `cmake --build . -j [--target X]` with `set -o pipefail`, teed to `<buildDir>/<target\|all>.output`. **Unbounded `-j`** — see *Parallelism* |
| `compile <buildDir> <file>` | compiles one TU: walks up from the file's mirrored dir to the nearest `Makefile`, resolves the object rule from it (`make <obj>.o`). Fails cleanly for a file no target owns |
| `buildProject <buildDir> <workspaceFolder> <relativeFile>` | `make -C <mirrored dir> -j$(nproc)` — the file's whole directory target |
| `buildTests <buildDir> <sourceDir> [base]` | builds `<base>.Tests` (`Jde.Opc.Gateway` → `Jde.Opc.Tests`), or everything with no base |
| `clean <buildDir>` | `cmake --build . --target clean` then deletes every `cmake_pch.hxx.pch` |

Callers: `linux-ci.yml` (`reconfig` then a raw `cmake --build -j $(nproc)` then `ctest`), and the VS Code tasks, which
get the build root from the `extensions/jde` command `jde.repoBuildDir` (`$JDE_BUILD_DIR/$JDE_COMPILER/<basename>`)
and append `/debug`.

```bash
source $JDE_DIR/build/buildFunctions.sh
B=$JDE_BUILD_DIR/$JDE_COMPILER/$(basename $JDE_DIR)/debug      # /mnt/ram/linux/clang++/opc-hub/debug

reconfig $B $JDE_DIR linux-clang-debug-jde       # new build dir, or after a preset/dependency change
cmake --build $B -j8                             # full build - NOT `build $B $JDE_DIR` (unbounded -j)
cmake --build $B -j8 --target Jde.DB Jde.DB.Tests
compile $B libs/fwk/src/io/json.cpp              # one TU, fastest check of an edit
clean $B
```

Raw equivalents (what the helpers wrap):

```bash
mkdir -p $B/runtime/logs
cmake -B $B -S $JDE_DIR -Wno-dev --preset linux-clang-debug-jde   # keep CMakeCache.txt = incremental reconfigure
cmake --build $B -j8 [--target <t>…]
cd $B && ctest --timeout 300 --output-on-failure                   # then the cpp-tests skill
```

Release: `R=…/<checkout>/release; cmake -B $R -S $JDE_DIR -Wno-dev --preset linux-clang-relWithDebInfo-jde -Djde_TESTS=OFF`,
then the targets the installer packs: `--target Jde.Opc.Hub Jde.Opc.Server Jde.DB.Sqlite Jde.DB.Sqlite.AppServer Jde.DB.Sqlite.OpcGateway Jde.DB.MySql`.

## Targets

`cmake --build $B --target help | grep '^\.\.\. Jde'` lists them. Names do not always match directories:

| kind | targets |
|------|---------|
| libraries | `Jde` (= `libs/fwk`), `Jde.DB`, `Jde.DB.MySql`, `Jde.DB.Sqlite`, `Jde.QL`, `Jde.Access`, `Jde.Web.Client`, `Jde.Web.Server`, `Jde.App.Shared`, `Jde.App.Client`, `Jde.Opc`, `Jde.Opc.HistorianLib` |
| sqlite native-proc modules | `Jde.DB.Sqlite.AppServer` (access procs live here too), `Jde.DB.Sqlite.OpcGateway` |
| app libraries | `Jde.App.ServerLib`, `Jde.Opc.ServerLib`, `Jde.Opc.GatewayLib`, `Jde.Opc.HubLib` |
| executables | `Jde.App.Server`, `Jde.Opc.Server`, `Jde.Opc.Gateway`, `Jde.Opc.Hub`, `Jde.Opc.PlcEmulator`, `Jde.Opc.Soak` |
| tests | `Jde.Fwk.Tests`, `Jde.DB.Tests`, `Jde.DB.Sqlite.Tests`, `Jde.DB.MySql.Tests`, `Jde.QL.Tests`, `Jde.Access.Tests`, `Jde.Web.Tests`, `Jde.App.Tests`, `Jde.Opc.Lib.Tests`, `Jde.Opc.Historian.Tests`, `Jde.App.Server.Tests`, `Jde.Opc.Server.Tests`, `Jde.Opc.Tests` (= `apps/OpcGateway/tests`), `Jde.Opc.Hub.Tests`, `Jde.Opc.PlcEmulator.Tests` |

odbc (`Jde.DB.Odbc*`) is Windows-only. The OpcServer, Gateway and Hub exes reuse `Jde.App.Server`'s PCH (`REUSE_FROM`), so that target builds first.

## The third-party tree

`build/CMakeLists.txt` is an ExternalProject superbuild, reached through the root CMakeLists with `jde_REPOS=ON`
(the `-repos` presets) — never configured directly. Pinned there: fmt 12.2.0, gtest 1.18.0, spdlog 1.17.0,
abseil 20260526.0 (patched: one monolithic shared lib, `ABSL_OPTION_INLINE_HW_ACCEL_STRATEGY=2`), ryml 0.10.0,
jsonnet 0.22.0 (patched), protobuf 35.1, open62541 1.5.9, open62541-nodeset-loader (patched `Nodeset.c`).
Each consumer echoes its deps' tags as `-Djde_DEP_TAG_<dep>`, so bumping a tag reconfigures the consumers.

```bash
cmake -B $REPO_DIR/build/debug -S $JDE_DIR -Wno-dev --preset linux-clang-debug-repos
cmake --build $REPO_DIR/build/debug -j8            # add --target open62541 etc. for one dep
```

- **Boost 1.92** (json + charconv, clang/libc++, ASan in Debug) and the **sqlite 3.53.4** amalgamation static lib are
  **not** in the superbuild: `build/boost-sqlite.sh` builds both into the install tree (re-runnable; it needs the two
  source trees unpacked under `$REPO_DIR`, URLs in its header).
- OpenSSL is the system `libssl-dev`; open62541's export links it **static**, which is why `-lzstd` and
  `-ljitterentropy` must resolve in every consumer (the CI Dockerfile builds jitterentropy from source).
- An incremental repos build can fail **once** at open62541's git update step after a tag bump (`deps/mqtt-c`
  "not our ref"); the tag is fetched by then, so re-run.
- After any `GIT_TAG` bump run `build/third-party-notices.sh` — the installers ship `THIRD-PARTY-NOTICES.txt`.
- A `-jde` build only needs the install tree, which is why CI bind-mounts `$REPO_DIR/install` read-only.

## Parallelism and where to run it

- **`-j8`, never bare `-j`** (user rule, 2026-09-05): `cmake --build $B -j8`. The `build` helper and the run-services
  skill's snippet use unbounded `-j`; don't call them for a full build.
- A full debug rebuild (ASan + debug info, ~13 GB on `/mnt/ram`) takes long enough at 8 jobs that it belongs in a
  **background Bash** with the log in the scratchpad; single-target builds are fine in the foreground.
- Build logs the helpers leave behind: `<buildDir>/cmake.output`, `<buildDir>/<target>.output`, `all.output`.

## Gotchas

- **Stale PCH after a clang update** — `PCH file '…/cmake_pch.hxx.pch' built from a different branch than the
  compiler` on every target: `/mnt/ram` is restored from a snapshot at boot, so the tree may predate the current
  apt clang-23 and make does not track the compiler. Fix: `find $B -name cmake_pch.hxx -exec touch {} +` then a
  full rebuild. Variant: `file 'cmake_pch.hxx' has been modified since the precompiled header … was built` on one
  target after a mid-build reconfigure — touch that target's `cmake_pch.hxx` and rebuild.
- `reconfig` **deletes the cache** — a full reconfigure, and every PCH/object rebuilds afterwards. For an incremental
  reconfigure (a CMakeLists edit) just re-run the raw `cmake -B … --preset …`, or let `cmake --build` do it.
- Direct runs of a binary need `cd $B/runtime` plus `REPO_SOURCE_DIR=$JDE_DIR REPO_BUILD_DIR=$(dirname $B)` —
  `REPO_BUILD_DIR` is the build dir's **parent**. ctest sets both itself; see the cpp-tests skill.
- `UA_NODE_SETS` unset → the OpcServer/Gateway/Hub nodeset-load tests fail, not the build.
- `MIN_REQ_CMAKE_VERSION not set` / `CMAKE_CXX_FLAGS lacks -march=…` at configure = a preset was not used.
- `linux-release.yml` configures with a raw `cmake -B` (no `reconfig`), so it does `mkdir -p "$BUILD_DIR/runtime/logs"`
  itself first.

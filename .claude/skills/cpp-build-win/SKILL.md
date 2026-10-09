---
name: cpp-build-win
description: How to configure and build the Jde C++ tree on Windows — the build-dir layout ($JDE_BUILD_DIR/$JDE_COMPILER/<repo>/<debug|release>) and the $REPO_DIR vs $JDE_DIR roles, the build/buildFunctions.sh helpers (reconfig/build/compile), the raw cmake equivalents and why -B is mandatory, the OS-split CMakePresets files with the -jde/-repos preset tables, compilers (clang++-23/libc++, g++-15 for deps only, C++26), and why there are no build or test presets. Use when configuring, building a target, or compiling a single file in this repo on Windows. (Linux: the cpp-build-linux skill.)
---

# Building (C++)

Build outputs go to `/mnt/ram/`. `$JDE_DIR` (= `$JDE_BASH`) is this checkout's source root; `$REPO_DIR` is the **third-party** root (`/home/duffyj/code/libs`) that the presets install dependencies under (`installRoot = $env{REPO_DIR}/install/$env{CXX}`) — it is not the jde repo. Each compiler+checkout pair gets an out-of-source build directory per build type: `$JDE_BUILD_DIR/$JDE_COMPILER/<repo-basename>/<debug|release>` (e.g. `/mnt/ram/linux/clang++/Public/debug`). The helpers in `build/buildFunctions.sh` take that full build dir as their first argument; `reconfig` creates it (plus `runtime/logs`) and copies the generated `compile_commands.json` to the source root for clangd (the build dir keeps its own copy too, for tools like VS Code's CMake Tools that expect it there).

```bash
source $JDE_DIR/build/buildFunctions.sh
buildDir=$JDE_BUILD_DIR/$JDE_COMPILER/$(basename $JDE_DIR)/debug   # /mnt/ram/linux/clang++/Public/debug

# Configure (wipes CMakeCache.txt, creates runtime/logs, copies compile_commands.json to the source root)
reconfig $buildDir $JDE_DIR linux-clang-debug-jde

# Build a target
build $buildDir $JDE_DIR Jde
build $buildDir $JDE_DIR Jde.Fwk.Tests

# Compile one file (resolves the object target from the build tree)
compile $buildDir $JDE_DIR/libs/fwk/src/io/json.cpp

# Raw cmake equivalents. -B is required: no Linux preset sets binaryDir, and preset mode ignores cwd.
cmake -B $buildDir -S $JDE_DIR -Wno-dev --preset linux-clang-debug-jde
cmake --build $buildDir -j --target Jde
cmake --install $buildDir
```

Linux uses **clang++-23** with libc++ (plus ASan/LSan in debug); Windows uses clang too. C++ standard is **C++26**. Every selectable configure preset (`cmake --list-presets`) carries one of two suffixes; everything else is a `hidden` building block.

The presets are **split by OS across four files** (presets schema `version: 9`). The auto-loaded `CMakePresets.json` only does `"include": ["CMakePresets.${hostSystemName}.json"]`, which resolves to `CMakePresets.Windows.json` or `CMakePresets.Linux.json` — capitalized to match `CMAKE_HOST_SYSTEM_NAME`, and case must match exactly since Linux is case-sensitive. Each OS file in turn includes `CMakePresets.common.json`, which holds the OS-agnostic hidden building blocks (`common`, `repos`, `debug`, `relWithDebInfo`, `release`, `clang`, `clang-jde`). So on any host `cmake --list-presets` shows only that OS's presets. `common.json` **must stay a separate file**: a preset may only inherit from its own file or a file it includes (inheritance flows downward along includes), so the shared blocks can't be folded up into the root — the OS files include the common file and inherit from it. `${hostSystemName}` is allowed in `include` because it is not a preset-specific macro (unlike `${presetName}`/`${generator}`), but that needs schema `version: 9`.

**`-jde`** — builds the project (libs, apps, tests), adding `CMAKE_EXPORT_COMPILE_COMMANDS` and the house warning exclusions. This is the day-to-day dev build:

| compiler | debug | relWithDebInfo | release |
|---|---|---|---|
| clang | `linux-clang-debug-jde` | `linux-clang-relWithDebInfo-jde` | — |
| win clang | `win-clang-debug-jde` | — | `win-clang-release-jde` |

The `-jde` presets do **not** set `jde_REPOS`/`jde_TESTS`/`jde_APPS` — they rely on the `option()` defaults in the root `CMakeLists.txt` (`OFF`/`ON`/`ON`), which only apply to a fresh cache. Re-running a `-jde` preset over a build dir last configured with a `-repos` preset keeps the cached `jde_REPOS=TRUE`, `jde_TESTS=FALSE`, `jde_APPS=FALSE`; the build then only touches the deps and ctest runs stale or no suites. The configure output's `jde_Superbuild=…,jde_REPOS=…,jde_TESTS=…,jde_APPS=…` line shows which you got. When switching a dir from `-repos` to `-jde`, pass `-Djde_REPOS=OFF -Djde_TESTS=ON -Djde_APPS=ON` (or delete `CMakeCache.txt`).

**`-repos`** — builds only the third-party dependencies (`jde_REPOS=ON`, `jde_TESTS=OFF`, `jde_APPS=OFF`):

| compiler | debug | relWithDebInfo | release |
|---|---|---|---|
| clang | `linux-clang-debug-repos` | `linux-clang-relWithDebInfo-repos` | — |
| win clang | `win-clang-debug-repos` | — | `win-clang-release-repos` |
| g++ | `linux-debug-repos` | `linux-relWithDebInfo-repos` | — |

The g++ (`linux-*`, **g++-15**) presets exist only for the dependency build — there is no g++ `-jde` preset, so the project itself is built with clang.

There are **no build or test presets** on either OS — `cmake --list-presets` lists configure presets only, and building/testing go through `cmake --build <buildDir>` and `cd <buildDir> && ctest`. Two vestigial `linux-debug` build/test presets were removed 2026-08-14: they named the hidden `linux-debug` configure preset, so `cmake --build --preset` and `ctest --preset` both failed with `Cannot use hidden configure preset`, and they could not have worked regardless since no Linux configure preset sets `binaryDir` (which is also why `reconfig` passes `-B`).

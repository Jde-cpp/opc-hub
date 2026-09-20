# GitHub-hosted Windows 2025 CI (deps cache + build)

Two workflows build the project on the **GitHub-hosted** `windows-2025` (VS2026)
image — no self-hosted workstation, everything is provisioned per run. They are
additive to and independent of the self-hosted [`win11-ci.yml`](workflows/win11-ci.yml).

| Workflow | File | Does |
|----------|------|------|
| **Win2025 Deps** | [`workflows/win2025-deps.yml`](workflows/win2025-deps.yml) | Builds the third-party libraries from [`build/CMakeLists.txt`](../build/CMakeLists.txt) (`win-clang-release-repos`) and saves them to `actions/cache`. |
| **Win2025** | [`workflows/win2025-build.yml`](workflows/win2025-build.yml) | Restores that cache and builds the repo's exe/DLLs (`win-clang-release-jde`), uploading them as an artifact. No tests. |

Both share [`actions/setup-windows-toolchain`](actions/setup-windows-toolchain/action.yml),
which installs the toolchain and — critically — computes the **one** deps cache
key both sides use, so it can never drift.

## What the image provides vs. what we install

The `windows-2025` (VS2026) image already has **CMake 4.4.0**, **Ninja 1.13.2**,
**vcpkg** (`C:\vcpkg`), and **VS2026 + Windows SDK 10.0.26100** (the MSVC target
and `dbgeng.lib` that clang++ links). The composite action adds the two missing
pieces:

- **LLVM 23.1.1** — the image ships LLVM 20.1.8, too old for this C++26 codebase
  (clang/lld 23 is required). Downloaded from the LLVM GitHub release
  (`clang+llvm-23.1.1-x86_64-pc-windows-msvc.tar.xz`) to `C:\llvm23`, cached by
  version.
- **Boost 1.91.0 source** — absent from the image; downloaded to
  `C:\jde\libs\boostorg\boost_1_91_0` (Windows compiles `boost_json` inline from
  source, no `find_package(Boost)`). Part of the deps cache.

## Fixed paths (set by the composite action)

| Var | Value | Holds |
|-----|-------|-------|
| `REPO_DIR` | `C:/jde/libs` | `install/clang++/Release`, `boostorg/boost_1_91_0` |
| `JDE_BUILD_DIR` | `C:/jde/build` | transient CMake build trees (not cached) |
| `VCPKG_ROOT` | `C:\vcpkg` (image default) | `installed/` (openssl, sqlite3 @ `x64-windows` - dynamic) |

## The deps cache

Paths: `C:/jde/libs/install/clang++/Release`, `C:/jde/libs/boostorg/boost_1_91_0`,
`C:/vcpkg/installed`.

Key (exact match, no `restore-keys`):
```
windows-deps-release-llvm23.1.1-boost1.91.0-<hash of CMakePresets*.json, build/CMakeLists.txt, build/functions.cmake, vcpkg.json>
```
`build/CMakeLists.txt` holds every dependency `GIT_TAG`, so bumping any dep
changes the hash and forces a fresh deps build.

## Bootstrap / order of operations (strict coupling)

The build workflow **fails fast** if the deps cache is absent — it never builds
deps. Therefore:

1. **First**, run **Win2025 Deps** on `main` (Actions → Win2025 Deps →
   Run workflow). This populates the repo-wide cache. Cache created on `main` is
   visible to all branches and PRs; a feature-branch run is only visible to that
   branch.
2. **Then** dispatch **Win2025** (on demand only — pick the branch to
   build) and it hits the cache.

If you bump a dependency `GIT_TAG` (or a preset / `vcpkg.json`) on a branch, the
key changes and the build workflow will strict-miss on that branch until
**Win2025 Deps** re-runs for it (dispatch deps on that branch, or merge to
`main` and dispatch deps there).

**Win2025 Deps** is manual-only — there is no automatic push/schedule
refresh. Re-dispatch it whenever a key input changes or the cache lapses the
7-day inactivity eviction, or the strict build workflow will fail on a miss.

## Notes / risks

- **Release is new to CI.** The self-hosted CI only exercises Debug; the Release
  `-repos`/`-jde` presets may surface first-run issues. The build step keeps the
  `-j 1` serial fallback for the in-source protobuf codegen race (see
  [`win11-runner.md`](win11-runner.md)).
- **LLVM 23 on hosted Windows is the biggest unknown** — if a future pinned
  version lacks the `...-x86_64-pc-windows-msvc.tar.xz` asset, switch the
  composite action to the `LLVM-<ver>-win64.msi` installer
  (`msiexec /i <msi> /qn INSTALL_ROOT="C:\llvm23"`) — 23.x ships an MSI, not the
  NSIS `.exe` earlier releases used.
- **Cache budget:** the three cached paths total a few GB against the repo's
  10 GB limit — fine for one Release config; watch it if more are added.

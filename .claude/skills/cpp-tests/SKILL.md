---
name: cpp-tests
description: How to run the Jde C++ GoogleTest suites — the required -settings and -tests/-ctest flags, the settings CLI flag table, which suites are sqlite-backed under ctest, the differing working directories for ctest vs direct runs, and how to filter down to a single test. Use when running, debugging, or configuring C++ tests in this repo.
---

# Running Tests (C++)

Tests use **GoogleTest**. Each test binary requires a `-settings=` argument pointing at a Jsonnet config. Per-library configs live at `libs/<lib>/tests/config/<Lib>.Tests.jsonnet` (and `apps/<app>/tests/config/<App>.Tests.jsonnet`).

A **test-mode flag is required** — `-tests` for direct runs, `-ctest` for ctest. They are equivalent except that `-ctest` also selects a compact console log pattern (`log/SpdLog.cpp`). One of them binds the `buildTarget`/`cwd`/`logsDir`/`windows` ext vars the configs read, and selects the default import dir — `config/args/mysql` on Linux, `config/args/sqlServer` on Windows. Without one, jsonnet evaluation fails and the binary starts with an `{"error":…}` settings object. Test output is cwd-relative: logs go to `<cwd>/logs` and file-backed sqlite dbs to `<cwd>/sqlite-tests.db`.

Settings-related CLI flags (`libs/fwk/src/settings.cpp`):

| flag | effect |
|------|--------|
| `-settings=<file>` | the Jsonnet config to load |
| `-tests` / `-ctest` | test mode, as above; `addJdeTest` passes `-ctest` |
| `-include=<dir>[;<dir>…]` | replaces the import dirs, each relative to the settings file's directory. **Takes priority over the `-tests`/`-ctest` default** — but does not bind the ext vars, so pair it with a test flag |
| `-arg <k>=<v>` | binds jsonnet ext var `k`; split on the *first* `=`, so values may contain more |
| `-sync` | sets the `sync` top-level argument to `true`, enabling startup DDL schema-sync (off by default — see the `function( sync=false )` heading in the app configs) |

The four db-backed ctest suites — `libs/access/tests`, `apps/OpcGateway/tests`, `apps/OpcServer/tests`, and `apps/OpcHub/tests` (the hub: AppServer + gateway + an OpcServer in one process, on 1973-1975/4842 so it runs beside a live hub) — are wired to sqlite on **every** platform: their `addJdeTest` call adds `-include=args/sqlite -arg path=:memory:`, so ctest needs no db server and writes no db file. (A direct, non-ctest `-tests` run of the same binary still takes the default mysql/sqlServer import dir, since `-include` is only on the ctest registration.) The `libs/fwk`, `libs/web` and `apps/OpcServer/emulator/tests` (`Jde.Opc.PlcEmulator.Tests` — the PLC emulator's generators, device parsing and its own UA server on loopback 4851, compiled from the emulator's sources) suites use no database, and `libs/db/drivers/sqlite/tests` is inherently sqlite. The `libs/db/tests`, `libs/db/drivers/mysql/tests`, and `libs/db/drivers/odbc/tests` suites open no data source at all — they assert on generators, `Value`, and the per-dialect `Syntax` implementations, so they need no server on any platform.

The two workflows use **different working directories**, so they keep separate logs/db files:

- **ctest**: `addJdeTest()` (`build/functions.cmake`) registers each suite with `WORKING_DIRECTORY $buildDir/Testing` (ctest creates it) and sets the `REPO_SOURCE_DIR`/`REPO_BUILD_DIR` env vars the configs expand via `$(…)` — output lands in `$buildDir/Testing/{logs,sqlite-tests.db}`.
- **direct/debugger runs**: run from `runtime/` (created by `reconfig`; the VS Code launch configs use it too) with `REPO_SOURCE_DIR`/`REPO_BUILD_DIR` exported in the shell — output lands in `$buildDir/runtime/{logs,sqlite-tests.db}`.

```bash
# Run a test binary directly
cd $buildDir/runtime
$buildDir/libs/fwk/tests/Jde.Fwk.Tests -tests -settings=$JDE_DIR/libs/fwk/tests/config/Framework.Tests.jsonnet

# Or every suite via ctest (addJdeTest passes -ctest and the env); --preset is broken, see reviews/todo.md §3
cd $buildDir && ctest
```

**Running a single test:** the `testing.tests` field in the Jsonnet config is the GoogleTest filter. Set it to e.g. `"FileTests.WriteRead"` (or a pattern like `"FileTests.*"`) to restrict the run. Some test binaries look for their config in `~/.Jde-Cpp/Tests.<Lib>/<Lib>.Tests.jsonnet` — symlink the repo config there if missing.

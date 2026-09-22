# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This is a monorepo for the **Jde OpcGateway** system — an OPC-UA gateway with REST/WebSocket API, Angular frontend, and C++ backend services.

## Building (C++)

See the **`cpp-build`** skill (`.claude/skills/cpp-build/SKILL.md`) for the full procedure — the build-dir layout and the `$REPO_DIR`/`$JDE_DIR` roles, the `buildFunctions.sh` helpers (`reconfig`/`build`/`compile`), the raw cmake equivalents and the mandatory `-B`, the OS-split presets with the `-jde`/`-repos` preset tables, and why there are no build or test presets.

## Running Tests (C++)

See the **`cpp-tests`** skill (`.claude/skills/cpp-tests/SKILL.md`) for the full procedure — the required `-settings` and `-tests`/`-ctest` flags, the settings CLI flag table, which suites are sqlite-backed under ctest, the differing working directories for ctest vs direct runs, and single-test filtering.

## Frontend (Angular)

See `web/CLAUDE.md` — it loads automatically when working under `web/` (the active workspace is `web/opc/my-workspace/`; `ng test` runs Vitest, not Karma).

## C++ Code Conventions

### Macro aliases (defined in `include/jde/fwk/macros.h` and `usings.h`)

| Macro | Meaning |
|-------|---------|
| `α` | `auto` (member function return) |
| `β` | `virtual auto` |
| `Ω` | `static auto` |
| `Ξ` | `inline auto` |
| `ψ` | `template<class... Args> auto` |
| `ι` | `noexcept` |
| `Ι` | `const noexcept` |
| `ε` | `noexcept(false)` |
| `Γ` | dllexport/visibility("default") (from `exports.h`) |
| `Φ` | `Γ auto` (exported function) |
| `SL` | `std::source_location` |
| `SRCE_CUR` | `std::source_location::current()` |
| `FWD(a)` | `std::forward<decltype(a)>(a)` |

### Type aliases (from `usings.h`)

- `sv` = `std::string_view`, `str` = `const std::string&`
- `sp<T>` / `up<T>` / `wp<T>` = shared/unique/weak ptr
- `uint` = `uint_fast64_t`, `uint32` = `uint_fast32_t`, etc.
- `uuid` / `StringMd5` = `boost::uuids::uuid`
- `jvalue` / `jobject` / `jarray` = Boost.JSON types
- `flat_map`, `flat_set` = Boost.Container types

### Logging macros (from `log/log.h`)

Each file typically defines `_tags` as its `ELogTags` value. Then:

```cpp
INFO("message {}", value);       // ELogLevel::Information, uses _tags
WARN("message");
ERR("message");
DBG("message");
TRACE("message");
INFOT(tags, "message");          // explicit tags variant
```

### Stacktrace / compiler portability

Use `#ifdef __cpp_lib_stacktrace` to branch between `std::stacktrace_entry` (GCC/C++23) and `boost::stacktrace::frame` (clang). Do **not** use `#ifdef __clang__` for this purpose.

### Coroutines

Awaitables inherit from `VoidAwait` or `IAwait<TResult, TTask>` in `co/Await.h`. Tasks use `VoidTask` and typed task types from `co/Task.h`.

**The pairing rule: an awaitable dictates its caller's return type.** `IAwait`/`VoidAwait` fix `await_suspend` to their **own** task's handle, and the awaited value is delivered through the *caller's promise* (`IExpectedPromise::Resume` stores it there; `TAwait::await_resume` reads it back) — so a coroutine may only `co_await` awaitables whose `::Task` **is** its own return type. Mixing two kinds in one coroutine fails a `static_assert` that states this rule and both fixes — a diagnostic-only wrong-handle `await_suspend` overload in `VoidAwait`/`IAwait` catches the mismatch that would otherwise surface as an unexplained `no viable conversion from 'coroutine_handle<…>'`. Corollaries:
- `await_ready` is not a coroutine — nothing needing a `co_await` can live there.
- The house workaround is the **hand-off chain**: one coroutine per awaitable type, each calling the next at its end, with state parked on the awaitable's *members*, not locals (e.g. `UpdateAwait::Build → UpdateBefore → Execute → UpdateAfter`).
- A `BlockAwait` reached *from* awaitable machinery is the smell that this rule was hit and worked around. A `BlockAwait` *implementing* a synchronous API (`LocalQL::Upsert`, `ScalerSync`) is fine — that is what it is for.

**The escape hatch — `co/AnyAwait.h`.** `AnyAwait<TResult>`/`AnyVoidAwait` carry their own result/exception storage and a type-erased `coroutine_handle<>`, so **any** coroutine can `co_await` them regardless of its return type; `Any( awaitable )` wraps an existing `IAwait`-family awaitable the same way (rvalue → owned, lvalue → referenced). Prefer these for new cross-type awaits instead of adding another hand-off chain. Because storage is local, a subclass may also pre-complete (set `_result`/`_error`, return `true` from `await_ready`) — impossible in the `IAwait` family. Discipline: `Resume`/`ResumeExp` may run the awaiter to completion inline and destroy the awaitable, so they must be the caller's last use of `this`.

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `$REPO_DIR` | Third-party/dependency root (e.g. `/home/duffyj/code/libs`) — **not** the jde repo. Presets install deps under `$REPO_DIR/install/$CXX/<buildType>`; `functions.cmake` reads Boost sources from `$REPO_DIR/boostorg` on Windows |
| `$JDE_DIR` | This checkout's source root (e.g. `/home/duffyj/code/jde/opc-hub`); used by shell scripts and VS Code workspace configs — CMake files use paths relative to `CMAKE_CURRENT_LIST_DIR` instead |
| `$JDE_BASH` | Same as `$JDE_DIR` |
| `$JDE_BUILD_DIR` | Build-output parent directory (e.g. `/mnt/ram/linux`); actual build dirs are `$JDE_BUILD_DIR/$JDE_COMPILER/<repo-basename>/<debug\|release>` |
| `$JDE_COMPILER` | Compiler subdirectory name under `$JDE_BUILD_DIR` (e.g. `clang++`) |

## Other Stuff
- Never suggest enclosing single-line `if` statements with braces.
- Never run an unscoped/filesystem-wide search (e.g. `find /`, or any search rooted above the repo). Always scope file searches to `C:\Users\duffyj\source\repos` or `$JDE_BUILD_DIR` (build outputs, e.g. `x:\build` on Windows) — or a narrower subdirectory of either. A full-drive search can run for tens of minutes burning CPU for no benefit. Prefer Glob/Grep over shelling out to `find`/`grep` in the first place.
- Markdown docs that cite repo files (reviews, findings, design notes) must link them per the **`md-file-refs`** skill — `` [`DBException.h:37`](../opc-hub/include/jde/db/DBException.h#L37) ``, never `file://` (the VS Code preview blocks it) or root-relative. Run its `linkify.py` over the doc when finishing it; no need to ask first.
- Keep code comments minimal; only comment non-obvious logic


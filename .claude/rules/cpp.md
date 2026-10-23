---
paths:
  - "**/*.h"
  - "**/*.cpp"
---

# C++ Code Conventions

## Macro aliases (defined in `include/jde/fwk/macros.h` and `usings.h`)

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

## Type aliases (from `usings.h`)

- `sv` = `std::string_view`, `str` = `const std::string&`
- `sp<T>` / `up<T>` / `wp<T>` = shared/unique/weak ptr
- `uint` = `uint_fast64_t`, `uint32` = `uint_fast32_t`, etc.
- `uuid` / `StringMd5` = `boost::uuids::uuid`
- `jvalue` / `jobject` / `jarray` = Boost.JSON types
- `flat_map`, `flat_set` = Boost.Container types

## Logging macros (from `log/log.h`)

Each file typically defines `_tags` as its `ELogTags` value. Then:

```cpp
INFO("message {}", value);       // ELogLevel::Information, uses _tags
WARN("message");
ERR("message");
DBG("message");
TRACE("message");
INFOT(tags, "message");          // explicit tags variant
```

## Stacktrace / compiler portability

Use `#ifdef __cpp_lib_stacktrace` to branch between `std::stacktrace_entry` (GCC/C++23) and `boost::stacktrace::frame` (clang). Do **not** use `#ifdef __clang__` for this purpose.

## Coroutines

Awaitables inherit from `VoidAwait` or `IAwait<TResult, TTask>` in `co/Await.h`. Tasks use `VoidTask` and typed task types from `co/Task.h`.

**The pairing rule: an awaitable dictates its caller's return type.** `IAwait`/`VoidAwait` fix `await_suspend` to their **own** task's handle, and the awaited value is delivered through the *caller's promise* (`IExpectedPromise::Resume` stores it there; `TAwait::await_resume` reads it back) — so a coroutine may only `co_await` awaitables whose `::Task` **is** its own return type. Mixing two kinds in one coroutine fails a `static_assert` that states this rule and both fixes — a diagnostic-only wrong-handle `await_suspend` overload in `VoidAwait`/`IAwait` catches the mismatch that would otherwise surface as an unexplained `no viable conversion from 'coroutine_handle<…>'`. Corollaries:
- `await_ready` is not a coroutine — nothing needing a `co_await` can live there.
- The house workaround is the **hand-off chain**: one coroutine per awaitable type, each calling the next at its end, with state parked on the awaitable's *members*, not locals (e.g. `UpdateAwait::Build → UpdateBefore → Execute → UpdateAfter`).
- A `BlockAwait` reached *from* awaitable machinery is the smell that this rule was hit and worked around. A `BlockAwait` *implementing* a synchronous API (`LocalQL::Upsert`, `ScalerSync`) is fine — that is what it is for.

**The escape hatch — `co/AnyAwait.h`.** `AnyAwait<TResult>`/`AnyVoidAwait` carry their own result/exception storage and a type-erased `coroutine_handle<>`, so **any** coroutine can `co_await` them regardless of its return type; `Any( awaitable )` wraps an existing `IAwait`-family awaitable the same way (rvalue → owned, lvalue → referenced). Prefer these for new cross-type awaits instead of adding another hand-off chain. Because storage is local, a subclass may also pre-complete (set `_result`/`_error`, return `true` from `await_ready`) — impossible in the `IAwait` family. Discipline: `Resume`/`ResumeExp` may run the awaiter to completion inline and destroy the awaitable, so they must be the caller's last use of `this`.

**Several at once — `co/AllAwait.h`.** `All( a, b, … )` starts several awaitables of either family, unwrapped, and resumes once with a `tuple` of their results (`std::monostate` for a void slot) or the first failing slot's exception in slot order once every slot has settled — nothing here can be cancelled, so it always waits for all; rvalue slots are owned, lvalues referenced, and the source location is the first slot's. The caller's return type is free, as with `Any`.

**Lazy work — `AnyVoidTask` (`co/AnyAwait.h`).** A coroutine returning `AnyVoidTask` runs only once some coroutine, of any return type, `co_await`s it; its end resumes that awaiter, rethrowing what escaped the body. Use it for work picked now and awaited later (`HistGroupMAwait`'s `before`/`after` pick a `HistHost` edit side), where a `VoidTask` would start at once, detached, and could answer only through a reference to its awaiter. Its body `co_await`s the Any family only (`Any()` for the rest). It owns its frame, so a task nobody awaits just destroys it.

## Free-function definitions

A function a header declares at namespace scope is defined in its `.cpp` by its qualified name, from an enclosing namespace — `namespace Jde::Opc{ α Gateway::ResolveThresholds( … )ι->Hist::Thresholds{ … } }`, as `gatewayStartup.cpp` and `GatewayQL.cpp` do — not by reopening `namespace Jde::Opc::Gateway` and writing the bare name. A qualified definition has to match a declaration, so a parameter or return type that drifts from the header is a compile error; a bare one silently declares a new overload, and fails only at link, or not at all. File-local helpers (`Ω`, declared in no header) stay unqualified in the namespace they serve, and members are already qualified by their class.
---
paths:
  - "**/*.jsonnet"
  - "**/*.libsonnet"
---

# Settings (Jsonnet)

A hidden key (`::`) in a settings file is a **disabled option that shows its default**: it holds the value the code uses when the key is absent, so the file shows what applies, and un-hiding it (`::` → `:`) changes nothing until the value is edited. `App.Server.jsonnet`'s `socketTimeout:: "P1D"` (`Sessions.cpp`'s `value_or( 24h )`), the historian's `delay:: "PT1M"` in `Opc.Gateway.jsonnet`, a tests config's `tests:: "*"` (`testMain.h`'s filter). Switches too: the tests configs' `sync:: false` and `embeddedOpcServer:: true` show the defaults, and turning one on means un-hiding it and flipping the value. A composed config may show it by reference, as `Opc.Hub.jsonnet`'s `socketTimeout:: app.http.socketTimeout`.
- Don't remove one as dead or inert config: it is the only place the default shows.
- A new option goes in hidden, with its default. When the code's default changes, the hidden value changes with it.
- This covers settings keys only. A `.libsonnet`'s hidden helpers and functions (`hub-common.libsonnet`, `introspection/scalars.libsonnet`) are not options.
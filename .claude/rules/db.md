---
paths:
  - "**/*.sql"
  - "libs/db/**"
  - "libs/ql/**"
  - "**/*-meta.jsonnet"
  - "**/*-meta.libsonnet"
---

# Database

An empty string is saved as null in a nullable text column. The QL engine does it for every stock create and update, in `JsonColumn::ToValue` (`libs/ql/src/types/JsonColumn.cpp`), whether the value is a literal or a variable's.
- A NOT NULL text column keeps `''`: null would be refused.
- SQL tests an unset text column with `is null` alone, never `=''`. SQL Server's `=` ignores trailing spaces, so `''` also matches `' '`, which C++ counts as a value.
- A write that bypasses the stock ops, such as a custom mutation's own statement or a proc called directly, stores null for `""` itself.
- Strings are not trimmed: `' '` is a value.
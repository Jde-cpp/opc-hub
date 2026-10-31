# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Building and Testing (C++)

Load the **`cpp-build-linux`** skill (`.claude/skills/cpp-build-linux/SKILL.md`; Windows: **`cpp-build-win`**, `.claude/skills/cpp-build-win/SKILL.md`) before configuring or building, and **`cpp-tests`** before running a suite — the build dirs, flags and presets are non-standard.

## Frontend (Angular)

See `web/CLAUDE.md` — it loads automatically when working under `web/` (the active workspace is `web/opc/my-workspace/`; `ng test` runs Vitest, not Karma).

## C++ Code Conventions

`.claude/rules/cpp.md` holds them — the Greek-letter macro and type aliases, the logging macros, the stacktrace `#ifdef`, **Coroutines** (the pairing rule the headers cite as `CLAUDE.md 'Coroutines'`: an awaitable dictates its caller's return type; `Any`/`All`/`AnyVoidTask` are the escape hatches) and qualified free-function definitions. It loads by itself when you Read or Edit a `.h`/`.cpp`; read it first if you write or review C++ any other way.

## Settings (Jsonnet)

A hidden key (`::`) in a settings file is a disabled option that shows its default: a new option goes in hidden, with its default, and none is ever removed as dead or inert config. The full convention is in `.claude/rules/jsonnet-settings.md`, which loads when you work with a `.jsonnet`/`.libsonnet` file.

## Database

The QL engine saves an empty string as null in a nullable text column, so SQL tests an unset text column with `is null` alone. The convention is in `.claude/rules/db.md`, which loads when you work with a `.sql` file or the db/ql libraries.

## Other Stuff
- Don't stage changes (`git add`, `git rm --cached`, `git restore --staged`) unless asked — leave edits in the working tree; the user stages and commits. Need a baseline to diff against? Snapshot the tree without touching the index (`GIT_INDEX_FILE=<scratch> git add -A && git write-tree`).
- Don't create branches — work on whatever branch is checked out; the user creates branches and lands changes through their own PR flow. (A branch made with `git switch -c <name> origin/main` tracks `origin/main`, so a plain push from it goes straight to `main`.)
- Never suggest enclosing single-line `if` statements with braces.
- Never run an unscoped/filesystem-wide search (e.g. `find /`, or any search rooted above the repo). Always scope file searches to `C:\Users\duffyj\source\repos` or `$JDE_BUILD_DIR` (build outputs, e.g. `x:\build` on Windows) — or a narrower subdirectory of either. A full-drive search can run for tens of minutes burning CPU for no benefit. Prefer Glob/Grep over shelling out to `find`/`grep` in the first place.
- Markdown docs that cite repo files (reviews, findings, design notes) must link them per the **`md-file-refs`** skill — `` [`DBException.h:37`](../opc-hub/include/jde/db/DBException.h#L37) ``, never `file://` (the VS Code preview blocks it) or root-relative. Run its `linkify.py` over the doc when finishing it; no need to ask first.
- Keep code comments minimal; only comment non-obvious logic
- Don't leave trailing empty lines in any file — it ends at its last character, with no final newline. No file type here needs one; when a tool appends one (Write, a heredoc, `print`), strip it.
- It is deployed to only a few sites, which can convert by hand, so no deployed database, historian file or config needs to survive a change. Don't flag, build or keep compatibility with data written by an earlier development build; dev boxes recreate theirs. This doesn't cover designing formats that can change after a release, such as a version field. Remove this note at the first release.
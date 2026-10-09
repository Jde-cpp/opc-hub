---
name: md-file-refs
description: How to cite repo files in markdown docs (code reviews, findings, design notes) so the refs are clickable in the VS Code preview — relative links with #L anchors, never file:// or root-relative — plus linkify.py, which converts and validates them. Use when writing or updating any markdown doc that cites source files by path:line.
---

# File references in markdown docs

Review/findings docs live in `reviews/` and `reviews-issue/` beside the checkouts (`~/code/jde/reviews/` next to `~/code/jde/opc-hub`), **outside** the repo — the VS Code workspace folder is the checkout itself (`jde.code-workspace` has one folder, `"."`). That placement is what breaks the two obvious link forms:

- **`file:///abs/path`** — the preview webview's CSP blocks it. The link renders but clicking does nothing.
- **Root-relative `/include/jde/db/DBException.h`** — resolved against the workspace folder, which the doc is not inside, so it lands somewhere arbitrary.

**Document-relative links are the only form that works**, because the preview resolves them against the doc's own directory:

```markdown
[`DBException.h:37`](../opc-hub/include/jde/db/DBException.h#L37)
```

`#L37` opens the file at that line. From `reviews/` the prefix is `../<checkout>/` (`../opc-hub/`, `../opc-hub2/`); for a doc inside the repo it is whatever `relpath` gives.

## Conventions

- **Label with the shortest unambiguous name, not the full path** — `` `DBException.h:37` ``, not `` `include/jde/db/DBException.h:37` ``. The full path is in the link target and shows on hover, so the label only has to identify the file. It stays a code span, so the doc reads as prose either way.
- **Extend the label only when the short name would be ambiguous**: another repo file shares the basename (`mysql/access_user_insert_key.sql` vs `sqlServer/…`), or a same-stem twin sits in a sibling directory (`sqlite/access_user_insert_key.cpp` — the backend dir *is* the distinguishing part). Header/source pairs across `include/` and `libs/` don't count: `DBException.h` and `DBException.cpp` are already distinct.
- **Anchor on the first line of a range.** `:13-26` → `#L13`, `:30/37` → `#L30`, `:24,31` → `#L24`.
- **Continuation refs inherit the file** from earlier in the same line: in ``` `DBException.cpp:29-34` vs `:36-42` ```, the bare `:36-42` links into `DBException.cpp`.
- **Don't anchor historical line numbers.** A ref to code that a fix has since replaced ("`DBException.cpp` old :27") should link the file only — a `#L27` there points at unrelated code.
- **Open findings should cite current lines.** If a fix shifted the code a ref names, update the number in the prose too; a closed/FIXED finding may keep its as-reviewed citation.

## linkify.py

Converts every `` `path:line` `` ref in a doc. Paths resolve against `git ls-files`, so bare basenames (`Entry.cpp`) and partial paths (`sqlServer/access_user_insert_login.sql`) work — no need to write full paths in prose.

```bash
python3 .claude/skills/md-file-refs/linkify.py ../reviews/<doc>.md
python3 .claude/skills/md-file-refs/linkify.py --check <doc>.md   # validate, exit 1 if broken
python3 .claude/skills/md-file-refs/linkify.py --dry-run <doc>.md
```

- The repo defaults to the doc's git toplevel, else `$JDE_DIR`; override with `--repo` (e.g. `--repo ../opc-hub2` for a doc reviewing that checkout).
- **Idempotent** — existing links are masked before substitution, so re-running after edits only picks up new refs. Run it again after any edit pass that adds refs.
- **Shortens labels on links already in the doc**, so prose may cite full paths and still come out short. A label is only rewritten when it resolves to the link's own target; hand-written labels (`[the login catch](…)`) are left alone.
- **Fenced code blocks are skipped**, so sample output containing paths is left alone.
- **Ambiguous basenames are left untouched and reported** (e.g. `Common.proto` exists in both `libs/app/shared/proto/` and `web/opc/proto/`). Resolve those by hand — write the disambiguating path in the prose (`libs/app/shared/proto/Common.proto`) and re-run, or link that one manually.
- `--check` also flags anchors past EOF, which is the cheap signal that a doc's line numbers went stale against the working tree.

Refs that aren't in backticks (`the QL funnel at :102-106`) are invisible to the script — link those by hand if they matter.

## Before handing a doc over

```bash
python3 .claude/skills/md-file-refs/linkify.py --check <doc>.md
```

Reports total links and any that point at a missing file or past EOF. Zero broken is the bar.

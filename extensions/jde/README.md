# Utilities

  - jde.repoBuildDir Appends the repository directory name to the build directory path ($JDE_BUILD_DIR/$JDE_COMPILER/&lt;repo&gt;)
  - jde.repoBuildRelDir Same, rooted at $JDE_RBUILD_DIR when it is set, otherwise $JDE_BUILD_DIR

A workspace can override the root with a `buildDir` setting - e.g. `repos.code-workspace`'s
`"buildDir": "${env:JDE_DEPENDS_BUILD_DIR}"` - which both commands read, so debug and release land under the one root.
The setting wins over the env vars and is the build root **verbatim**: neither $JDE_COMPILER nor the repo name is
appended, since the dependency tree is a flat `&lt;root&gt;/&lt;debug|release&gt;`.  `${env:VAR}` and `${workspaceFolder}` are
expanded (VS Code only does that for tasks.json/launch.json, not for settings read through the API) and a relative
value resolves against the checkout; an unset `${env:VAR}` is an error rather than a silently truncated path.

# C++ grammar

Replaces VS Code's built-in `source.cpp` grammar with a copy that fixes lambda trailing return types: the built-in matches
the type as `\S+`, so a lambda's `)->UA_Duration{` swallows the `{` and every line after is colored as a type until a
`{` preceded by whitespace.  `npm run grammar` (run by `vscode:prepublish`) writes the copy to `syntaxes/` from the
installed VS Code's grammar - pass its path to use another - so repackage after a VS Code update to pick up upstream changes.

# Formatter

This Formatter adds a space [after first]|[before last] bracket.  No spaces inbetween.
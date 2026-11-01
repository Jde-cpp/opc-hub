# CLAUDE.md — `web/`

Guidance for the Angular side of the repo:  its style (Angular style, Conventions) and what is specific to its layout.  Put
durable frontend guidance here - this file is tracked, and the workspace has no CLAUDE.md of its own.

## `my-workspace` is generated output, not source

`web/opc/my-workspace/` is scaffolded by `web/framework/scripts/create-workspace.sh`
(`ng new … --ai-config=none`) and every file in it is untracked — `.gitignore` carries
`**/my-workspace*/**`. It is recreated from scratch, so nothing authored directly inside it
survives. The tracked sources are linked in:

| tracked source | appears in the workspace as | link |
|---|---|---|
| `web/<lib>/control/src/{lib,public-api.ts,styles,assets}` | `my-workspace/projects/jde-<lib>/src/…` | symlink |
| `web/opc/site/**` (except `assets`) | `my-workspace/src/**` | **hard link** (`web/opc/scripts/setup.sh`) |
| `web/opc/site/assets` | `my-workspace/src/assets` | symlink (`setup.sh`) |
| `web/proto` | `my-workspace/proto`, mapped as the `jde-proto/*` tsconfig path | symlink |

Editing **through a symlink is fine** — the write lands in the tracked file under
`web/<lib>/control`, and `ng serve` picks it up without a restart.

Editing a **hard-linked** `my-workspace/src` file is not: any tool that replaces the file rather
than writing it in place (most editors, and the Edit tool) breaks the link, and the change
silently stops being tracked. Prefer editing `web/opc/site/…` directly; if you did edit the
workspace copy, re-link it with `ln -f <site-file> <workspace-file>` or re-run
`web/opc/scripts/setup.sh`.  Its `ng build` keeps unhashed output names (`main.js`) unless run as
`setup.sh --release`, which the workflows' tag runs do; the installers never pack the `*.map` files.

`preserveSymlinks` is set in both `angular.json` and `tsconfig.json` and must stay — without it
tsc and esbuild resolve the library sources to their real paths outside the workspace, which
breaks compilation and the sass `node_modules` lookup.

## Help content

The `/help` section renders markdown at runtime. Each library keeps its own under `web/<lib>/control/src/assets/help/*.md`
and the site keeps `overview.md`/`about.md` under `web/opc/site/assets/help/`. `create-workspace.sh` adds one angular.json
`assets` entry per library that has an assets dir (`projects/jde-<lib>/src/assets` → `assets/jde-<lib>`) and `setup.sh` one
for the site (`src/assets` → `assets/site`), so a file is served at `assets/<jde-lib|site>/help/<file>.md`. Topics are
described by `HelpTopic` constants each library exports (`spaHelpTopics`, `frameworkHelpTopics`, …) and registered in
`app.config.ts` under the `HELP_TOPICS` multi token, in display order; a topic's `routes` patterns are what the navbar's `?`
button matches the current url against.  A page split out of another's has a two-segment id (`gateways/groups`, served at
`/help/gateways/groups` by the `:topic/:subtopic` route), so a link to a topic is `helpTopicRoute( topic )`, never
`['/help', topic.id]`, which would carry the `/` as `%2F`. `marked` renders the page and must only ever be imported dynamically
(`await import('marked')`): a static import in a file `main` reaches would put it in the initial bundle, and even in
`help-page.ts` it would join that page's chunk, which every tab preloads (`PreloadAllModules`) whether or not it opens help.
`{{version}}` in the markdown is the `JDE_VERSION` constant setup.sh passes through the builder's `define` option, read via
`IEnvironment.get('version')`.

## Add-ons load by URL

Highcharts Stock (the History trend) is an add-on the install never carries (#302): its licence is a commercial one. `highcharts`
is a `devDependency` of jde-opc for the typings and `hist-trend.spec.ts` alone (`jde-opc.sh` installs it `--save-dev`); the
runtime is imported by URL, `addons/highcharts/esm/{highstock,modules/accessibility}.js` under the site's base href, by
`HighchartsLoader` (`node-history/highcharts-loader.ts`), which resolves `undefined` when the folder is absent - the trend then
shows a notice and the tab works from the table - and rejects, uncached, when it is there and failed to load. A fetch of
`highstock.js` after the failed import tells the two apart: a 404 or a page in its place is absent. A dev `setup.sh` fills
`my-workspace/public/addons/highcharts/esm/` from node_modules, which `ng serve` and `ng build` serve at the root. It skips this
when `$CI` is set, since a release workflow run by hand builds without `--release` and uploads the dist as a public artifact.
`setup.sh --release` never fills it. Every build fails if anything under `browser/` but `addons/` contains `Highsoft` (every
Highcharts file header carries it, so never write that word in a source), so a pull request's CI run catches Highcharts bundled by
mistake. `build-deb.sh` and the NSIS script exclude `addons/` besides. The trend alone calls `load()`, and how the load settled is
the loader's `state` signal, which the History toolbar reads too. Specs subclass `HighchartsLoader` and override `importFile`, its
URL import: with literal `import('highcharts/esm/…')` specifiers, which esbuild bundles into the test only, or with a rejection
and `fetch` stubbed for the absent and failed cases. A component that only reads the state gets `{state: signal(…)}`.

## Libraries

Four libraries plus the `my-workspace` application. Dependencies run one way:

```
jde-spa  →  jde-framework  →  jde-access  →  jde-opc
```

with `jde-proto` (generated, `web/proto`) consumed by `jde-framework` and `jde-opc`.
`jde-proto` is a plain package, not an Angular library — it has no `dist`, is imported by bare
specifier (`jde-proto/App.FromServer`) so ng-packagr externalizes it, and must never be copied
back inside a library.

## Routed pages are loaders, not barrel exports

`app.routes.ts` imports every library barrel statically (resolvers, services, table settings), so
`loadComponent: ()=>import('jde-opc').then( m=>m.Page )` splits nothing off: a dynamic import of a barrel needs its whole
namespace, and every page the barrel exports lands in the initial bundle (#351 - 1.99 MB against a 2 MB budget; the
budget now errors at 1.4 MB, so a page sliding back into `main` fails the build). Each library instead exports its routed
pages as loaders from one file, `lib/pages/<lib>-pages.ts` (`opc-pages.ts`, `access-pages.ts`, `framework-pages.ts`,
`spa-pages.ts`), and the barrel does not export the page class:

```ts
export const loadNodeDetail = ()=>import( './node-detail/node-detail' ).then( m=>m.NodeDetail );
```

The route then says `loadComponent: loadNodeDetail`. A page stays out of the initial bundle only while nothing in that bundle
imports the page's *file*, so:

- what the site or a resolver needs by name - a `tableSettings` constant, a route service, a list route - lives in a file of
  its own beside the page (`gateway-table-settings.ts`, `cards/home-route-service.ts`, `hist-admin/hist-admin-list-route.ts`),
  never in the page's;
- an unused component is not exported from the barrel - that alone kept `node-children.ts` in `main`;
- a component another library embeds by name (`QLList`, in jde-opc's gateway page) keeps its export, and still splits as long as
  every importer is itself lazy.

Check with the metafile: `ng build my-workspace --stats-json` writes `dist/my-workspace/browser-stats.json`; a page's `.ts`
must be an input of a lazy output, not of `main` or of a chunk `main` imports statically (`imports[].kind ==
"import-statement"`). A build that fails the budget writes no metafile - raise `maximumError` in the generated `angular.json`
from a backup and restore it afterwards.

## Shared styles

A component sheet cannot `@use` another library's partial, so a rule two libraries share is a global class in
`spa/control/src/styles/_general.scss` - `.jde-list-toolbar`, `mat-table.jde-dense`, `.jde-rights-grid`, `.jde-empty` - with
the explanation there once;  qualify it with the element (`mat-toolbar.`) where Material's own rule would win the cascade.
Within one library a partial does it (`opc/control/src/lib/pages/_status.scss`'s mixins, `_list-settings.scss`), and a
sheet two components of a library share goes in both `styleUrls` (`hist-admin-shared.scss`).

The breakpoints are named in `spa/control/src/styles/_constants.scss` alone (720 extra-small, 959 small, 1024 medium), so a
rule that depends on the **viewport** lives in jde-spa, the one library whose sheets can `@use` it: in `_general.scss` for a
global class (the list toolbar's wrap), else in a jde-spa component sheet (the navbar, the sidenav). TypeScript reads a breakpoint
through the custom property `_general.scss` publishes (`--jde-breakpoint-small`, read by `ScreenSize.isSmall`), never from a
number of its own. A rule that depends on the **room a component has** is a `@container` query in the component's own sheet -
`:host{ container-type: inline-size }` styling its descendants (`paginator.scss`, `hist-threshold-fields.ts`) - or fluid math
(`min()`, `clamp()`) when it is the host itself that changes (`detail-actions.scss`). A `--jde-*` custom property carries a value
for other sheets to compute with (`--jde-form-max-width`), never a condition: a media or container query cannot read one.
`.main-content` is not made a container: inline-size containment brings layout containment, and #329 relies on a table wider
than the pane overflowing its host to scroll the pane sideways.

## Angular style

The Angular CLI's own `--ai-config` template is not generated:  it changed with each CLI and overruled the repo's rulings.  These
are the rules of it the repo holds, and where the repo differs.
- Standalone components, and none sets `standalone: true` or `changeDetection` - OnPush is the default.
- `input()`, `output()` and `model()` (for a two-way `[(x)]`), never the decorators;  signals for state, `computed()` for what
  derives from it, and `set`/`update`, never `mutate`.
- Native control flow (`@if`, `@for`, `@switch`);  `class` and `style` bindings, not `ngClass`/`ngStyle`;  a component imports
  the directives and pipes its template uses, never `CommonModule`.
- Host bindings and listeners go in the decorator's `host` object, not `@HostBinding`/`@HostListener`.
- `inject()`, never constructor injection;  a new root singleton is `@Service()`.
- Prefer Signal Forms (`@angular/forms/signals`) for a new form;  never template-driven.
- It passes AXE and meets WCAG AA:  focus management, contrast, ARIA - and no positive `tabindex`.
- Where the repo differs from the CLI:  type strictness stays off, and a value a page must load before it renders starts as
  `null as any` (otherwise `unknown` over `any`).  Components keep their template and styles in files of their own
  (`templateUrl`, `styleUrl`) as a rule, inline only when small.

## Conventions

- Throw an `Error`, never a bare string - `throw new Error( … )`:  a string carries no stack, and the `instanceof Error` tests
  miss it.  `errorText` (`framework/control/src/lib/utils/errors.ts`) still reads a string, for what a library or the server
  hands back.
- A dependency is `#x = inject( T )`, with no type repeated - `inject` infers it from the class or the typed token.  One a
  template, a spec or another class reads is a plain field, `x = inject( T )`, and one a subclass reads is `protected`.
- Indent with tabs, in `.ts`, `.html` and `.scss` alike.
- A resolver is a class implementing `Resolve<T>`, provided on its route;  `helpTopicResolver` is the one `ResolveFn`.

## Commands

Run `ng` from `web/opc/my-workspace`.

- **`ng test` runs Vitest**, not Karma (`@angular/build:unit-test`). It runs every project; `ng test <lib>` runs one. Library
  specs live next to the code in `web/<lib>/control/src/…` and are picked up through the symlink — but only because each
  library's test target is given `buildTarget: my-workspace:build` (by `create-workspace.sh`), which is how the unit-test
  builder inherits `preserveSymlinks`. Without it esbuild realpaths every spec out of the workspace and the target silently
  runs nothing. A library with no spec files errors with "No tests found" and fails bare `ng test`, so keep at least one.
  The environment is jsdom (`ng new` adds it as a devDependency, and the builder picks it), so `window` and a working
  `localStorage` are globals - a spec needs no polyfill for either.
- **Spec helpers** live in each library's `control/src/testing/index.ts`, imported as `jde-spa/testing`, `jde-framework/testing`
  and `jde-opc/testing` - one tsconfig path, `jde-*/testing`, that `create-workspace.sh` writes beside `jde-proto/*` and links
  the folders for. They are never exported from a public-api, and the library builds exclude `src/testing/**`. A spec imports
  its own library's helpers and those below it:  the route, router, title and profile stubs, `flush`/`stable` and `Blank` from
  jde-spa;  the detail-page harness, `openRoute`, `snackbarStub`, `dialogStub`, `qlAnswering` and the schema builders from
  jde-framework;  `TestGateway`, `fakeGateway` and the node and historian fixtures from jde-opc.
- **A spec is written one way:**
  - `describe`, `it`, `expect` and `vi` are vitest globals, so nothing imports from `'vitest'`.  A mock's type is
    `ReturnType<typeof vi.fn>` or its helper's.
  - Arrows are `()=>{` and `x=>x.y`, with no spaces around `=>`.
  - TestBed is configured in a factory each `it` calls (`create`, `configure`, `open`), never in `beforeEach`, and the factory
    returns what the test reads:  the subject, its fixture and the stubs it asserts on.  Nothing is a `let` at module or
    `describe` level, or a `static` array or counter on a stub class;  a stub component is read off the fixture
    (`By.directive`).  A `beforeEach`/`afterEach` pair is for the environment alone:  console silencers, `vi.stubGlobal`,
    fake timers, `localStorage`.
  - TestBed resets itself between tests, so `TestBed.resetTestingModule()` is only where one `it` builds twice, saying so.
  - Waiting is `flush( turns )`, or `stable( fixture, passes )` for a page - not a `setTimeout` promise,
    `fixture.whenStable()`, `await Promise.resolve()` chains or `vi.waitFor`.  Fake timers are for code that sets a timer of
    its own, and `TestBed.tick()` runs effects where there is no fixture.
  - A collaborator a test asserts on is a `vi.fn()` or `vi.spyOn`, read through `.mock.calls` or `toHaveBeenCalled…`, not a
    hand-rolled `calls.push`/`++count`.  An output's or an observable's emissions may still be collected in an array.
- `ng build <lib>` works, but only in dependency order — a library whose dependencies are not yet
  in `dist/` fails with `Cannot find module 'jde-spa'`. Build `jde-spa` first, or just build the
  application (`ng build my-workspace`), which compiles every library from the symlinked sources.

## Verifying in the browser (claude-in-chrome)

The extension drives the user's own Chrome profile, which is signed into Google as the app's owner account. Nothing needs
configuring — no Claude Code permission rule, no extension site setting — but the sign-in only happens on `/login`, so
**navigate the tab to `http://localhost:4200/login` first** (backend up per the `run-services` skill — `driver.sh start
appserver gateway` or `hub` — and `ng serve` on 4200). Google Identity Services runs with `auto_select` and the saved
`googleLoginHint`, so the sign-in completes with no click in ~3–5 s. Confirm it, then go to the route under test:

```js
JSON.parse( localStorage.getItem('user') )?.email   // the owner's address when signed in (works on any route)
```

- After the sign-in the page may stay on `/login` (silent renewal) or land on `/` (credential path) — don't infer
  anything from that; check `localStorage.user` / `user()` and navigate explicitly to the route under test.
- The One Tap / FedCM prompt is browser UI and never appears in a CDP screenshot; read state through `javascript_tool`
  (dev mode exposes `ng.getComponent`), not screenshots. Screenshots inside a detail page's `mat-tab`s also time out
  with the page alive, and the first click after a navigation often only hovers — click again.
- Never type Google credentials. If `user()` is still `undefined` after ~10 s, stop and ask the user to sign into Google
  in that Chrome profile once by hand — Google blocks *interactive* sign-in under any DevTools-driven browser; only the
  already-signed-in silent path works.
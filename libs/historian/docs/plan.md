# Historian implementation plan — 2026-09-29

A plan for [`spec.md`](spec.md) ("last edited 2026-09-29"), written after both spec reviews were ruled and folded. It starts from the owner's proposed order:

1. Implement in OpcServer.
2. Implement open62541 calls in the gateway to query OpcServers, and QL queries for them.
3. Implement the web interface for those QL queries.
4. Implement the historian in the gateway/hub.
5. Implement the web interface for the gateway/hub features added on top of open62541.

That order is kept: OpcServer first and the gateway historian last. The plan changes three things:

- **Phase 0.** Some prerequisites are needed by both hosts. They land first, as small PRs that are each useful alone, so the OpcServer work doesn't start by pulling them in.
- **Step 2 was not in the spec.** The spec's `hist(group, …)` read only the gateway's own files, and the gateway never sent HistoryRead to a server. The pass-through was drafted in this plan's appendix and, once its five points were ruled, folded into the spec as *Pass-through* on 2026-10-07 ([#213]).
- **Steps 1–3 are built one capability at a time, not one layer at a time** (*Slices*, below). Raw reads should work end to end (library → OpcServer → gateway → web) before edits or aggregates start. That way the QL result shape and continuation are tested by a real user of them before Phase 5 has to match them.

Status is tracked on GitHub. [#193] is the parent issue, each phase below names its own issue, and slices A, B and C are milestones. The pass-through's rulings and fold went through [#213].

## Phase 0 — shared prerequisites ([#194])

Each item is its own PR, useful without the historian.

| # | Item | Why now |
| --- | --- | --- |
| 0.1 [#195] | **`Value` move** (spec *Values*, [`spec.md:75`](spec.md#L75)). Move `Value` from [`Opc.FromServer.proto`](../../../apps/OpcGateway/src/types/proto/Opc.FromServer.proto) into [`Opc.Common.proto`](../../../libs/opc/src/proto/Opc.Common.proto), and move that file from the gateway into `Jde.Opc`. Add `LocalizedText`, `QualifiedName`, `ExtensionObject` (type id and body), and `Array` (`repeated Value` plus dimensions). Add `Variant`↔`Value` conversion in both directions. Regenerate the web's generated code in `web/opc/control` (the current tree) and in `web/opc/proto`. | Both hosts encode with it, and the pass-through decodes with it. `/opc` gains the new types immediately, and the web shows them instead of `BadNotImplemented`. Round-trip every built-in type in `libs/opc/tests`' `ValueTests`/`VariantTests`. |
| 0.2 [#196] | **CRC-32C.** Add `IO::Crc::Calc32c` beside `Calc32` in [`crc.h`](../../../include/jde/fwk/io/crc.h): a compile-time table, and `absl::ComputeCrc32c` at run time. Abseil becomes one shared library, built on every preset with `cpuFlags`, which turn on SSE4.2 and PCLMULQDQ, so the run-time path is its hardware CRC. | Needed for checkpoints and `.flushed`. Test both paths against the standard vector, `"123456789"` → `0xE3069283`. |
| 0.3 [#197] | **Build flag.** Pin `-DUA_ENABLE_HISTORIZING=ON` in [`build/CMakeLists.txt`](../../../build/CMakeLists.txt). | Today it is only open62541's default. |
| 0.4 [#198] | **`Authorize` keeps names current.** `AccessListener::UserChanged` ([`AccessListener.cpp:49`](../../../libs/access/src/AccessListener.cpp#L49)) passes the event's name to `CreateUser` and applies `Updated`. | OpcServer's edits store `user_name` through `OpcAuthorize` too, so this can't wait for the gateway. |

## Phase 1 — `libs/historian`, host-agnostic ([#199])

**Host interface first.** Building for OpcServer first risks baking in its shape: one group, indexes it issues itself, and no database. So the library's seams are fixed before any storage code, and the unit tests drive them in both shapes from the start, including a multi-group fixture ([#201]).

| Concern | OpcServer | Gateway |
| --- | --- | --- |
| Thresholds | the nodeset's HA Configuration, plus defaults | `hist_group_nodes` → template member → group |
| node\_index | issued by the historian, carried in an `Issued` record after each value file's `FileStart` | the `hist_group_nodes` row id |
| Groups | one, `server` | many, named by `guid` |
| Break | stop or crash | also the connection-state callback |
| Identity | `OpcAuthorize` | `Authorize` through QL creds |
| Enqueue from | open62541's service lock | the connection's strand, under the monitoring lock |
| Clock and timers | injected | injected |

**Build order.** Each step merges with its own tests. Steps 1–4 are [#202]–[#205] under [#200], step 5 is [#206], and steps 6–7 are [#207].

1. **Records and I/O.** The `HistoryRecord` oneof, writes straight into the buffer, a reader over offset and limit, delta times, checkpoints, the first-open scan through the last good checkpoint, and the first append's truncation there - since #308 the framework's record layer ([`include/jde/fwk/io/records`](../../../include/jde/fwk/io/records/Framing.h)), with the historian's record traits over it. Tests truncate a live file at every byte offset, append a zero-filled tail, and garble a body with its length intact.
2. **Day files and durability.**
   - Layout: the `hist.path` lock, which disables the historian instead of exiting, and the two-slot `.flushed` file.
   - fsync of files and directories.
   - Per-group buffers: 8 KB, `delay` and `maxBuffer`, with drop, marker and write-back.
   - The in-memory list of live runs.
   - The midnight rewrite as a merge of runs, late-record merges into archives, and start-up recovery (stale temp files; generation-0 rewrites from the day `.flushed` names).
3. **Compression, heartbeat, gaps.** The band per format, the `MinTimeInterval` pending value, the heartbeat timer with the node's offset, and dropping a heartbeat on an earlier change with the flush hold-back. The gap comparison against the last delivered value, and after a restart against the newest record on disk.
4. **Raw reads.** Forward, reverse and open-ended reads; merging a live file's runs; bounds, including the forward scan of later preambles; the stateless continuation with its generation check; `readLimit`.
5. **Edits.** Flush before the edit, `Modification` records, fsync before the acknowledgement, modified reads, and start-value maintenance (in memory, the walk back, preamble corrections).
6. **ReadAtTime.**
7. **Aggregates.** The ten the spec lists, under Part 13's `AggregateConfiguration` defaults.

**Test harness.** An injected clock runs midnight, `delay` and heartbeats without waiting, and covers an IANA `timeZone` across a DST change. The strongest check is a model-based test. A naive oracle keeps every record in memory and applies edits literally. Random sequences of values, breaks, restarts, late records and edits then go to both the library and the oracle, and every read is compared. Most of the spec's hard cases (start-value correction, bounds after deletes, reverse paging) are interactions that hand-written cases miss.

## Phase 2 — OpcServer host, step 1 ([#208])

- **Backend.** [`UAConfig.cpp`](../../../apps/OpcServer/src/UAConfig.cpp) installs the `UA_HistoryDatabase` with `context` set. It sets the capability flags, and `maxReturnDataValues` from `hist.readLimit`. A flag is set only for what has merged (*Slices*).
- **Collection.** `setValue` records historizing nodes only, enqueues, and stamps a missing server timestamp.
- **Load.**
  - `Historizing` from the nodeset, written with `UA_Server_writeHistorizing` if the loader drops it.
  - HA Configuration, with defaults filled in.
  - `HistoryRead`/`HistoryWrite` ORed into `AccessLevel`.
  - `AggregateConfiguration` and `ServerTimestampSupported`.
  - `HistoryServerCapabilities` with its `AggregateFunctions` folder, and Median in OpcServer's own namespace.
  - `StartOfArchive`/`StartOfOnlineArchive`, at start and at each midnight.
- **node\_index.** Carried forward in an `Issued` record after each value file's `FileStart`. A node the nodesets no longer historize gets a `NodeRemoved`.
- **Access.** Read callbacks check each node's Read. The two history-update callbacks at [`UAAccess.cpp:575`](../../../apps/OpcServer/src/access/UAAccess.cpp#L575) answer Update and Delete on the node's enforced governing resource and refuse where none governs it, and `HistoryWrite` is left out of an unprotected node's `UserAccessLevel` (the spec's *Authorization*, [#237]).
- **Bounded callbacks.** Each node's read stops at `maxReturnDataValues` or one day file, then returns a continuation point. Measure how long a callback holds the service lock under emulator load.
- **Config.** A `hist` block in [`Opc.Server.jsonnet`](../../../apps/OpcServer/config/Opc.Server.jsonnet); `path` defaults to `hist/opc-server`.
- **Test data.** Mark a handful of variables in [`pumps.NodeSet2.xml`](../../../apps/OpcServer/config/nodesets/pumps.NodeSet2.xml) `Historizing`, with HA configurations that cover every deviation format, a `MinTimeInterval` and a heartbeat. The PLC emulator's signals (sine, ramp, random walk, toggle) then exercise compression live; soak with it.
- **Verification.**
  - `apps/OpcServer/tests` calls open62541's synchronous client helpers (`UA_Client_HistoryRead_raw` and the rest), which are fine in a test.
  - UaExpert's history trend and history view check Part 11 behaviour with a client we didn't write.
  - Phase 3 then adds automated end-to-end tests through the gateway.

## Phase 3 — gateway pass-through, step 2 ([#212])

The design is the spec's *Pass-through* section, whose five points were ruled and folded on 2026-10-07 ([#213]). The work:

- **Awaits.** `HistoryReadAwait` and `HistoryUpdateAwait` go beside `ReadAwait`. They send through open62541's generic async service, [`__UA_Client_AsyncService`](https://github.com/open62541/open62541/blob/v1.5.9/include/open62541/client_highlevel_async.h#L60), the gateway's first use of it; `ReadAwait` uses the typed `UA_Client_sendAsyncReadRequest` ([`ReadAwait.cpp:226`](../../../apps/OpcGateway/src/async/ReadAwait.cpp#L226)). The high-level helpers ([`UA_Client_HistoryRead_raw`](https://github.com/open62541/open62541/blob/v1.5.9/include/open62541/client_highlevel.h#L166) and the rest) are synchronous and take one node, which would block the strand. The request and response wrappers are move-only and deep-copy their NodeIds, as `ReadRequest` does ([`ReadAwait.h:8`](../../../apps/OpcGateway/src/async/ReadAwait.h#L8)).
- **Dispatch.** `GatewayQLAwait` routes `history`, and `GatewayQLMAwait` the edits, when they carry `opc`. `needsClient` already opens the caller's client for any query with `opc` ([`GatewayQLAwait.cpp:45`](../../../apps/OpcGateway/src/ql/GatewayQLAwait.cpp#L45)), the same way `updateVariable` gets it ([`VariableQLAwait.cpp:11`](../../../apps/OpcGateway/src/ql/VariableQLAwait.cpp#L11)).
- **Shared code.** The continuation codec (time, per-node counts at that time, argument hash) and the QL result serializer live where Phase 5 reuses them, not in the pass-through.
- **Tests.** The gateway tests already embed OpcServer in-process ([`OpcGateway/tests/main.cpp:29`](../../../apps/OpcGateway/tests/main.cpp#L29)), so they read Phase 2's historizing pump nodes:
  - paging across day files, forward and reverse;
  - a two-node read where one node is dense and one sparse, which exercises the merge horizon;
  - a connection dropped between pages, whose continuation still resumes because it is by time;
  - every edit type, followed by a modified read.

## Phase 4 — web for pass-through, step 3 ([#217])

- **Charting library.** There is none in [`opc/control/package.json`](../../../web/opc/control/package.json) today, so choosing one comes first. It needs stepped and interpolated series, a time axis that pans into more pages, and point markers for status. Chosen in 4A ([#218]): Highcharts Stock, a peer of `jde-opc` installed by [`jde-opc.sh`](../../../web/opc/scripts/jde-opc.sh) and loaded on first use, never in the initial bundle.
- **One history service** takes a source, `{opc}` or `{group}`. Phase 4 implements `opc`, and Phase 6 adds `group` without touching the components.
- **Components.**
  - A trend: stepped by default, gaps drawn where status is Bad; "load earlier" is a reverse read with only `end`.
  - A table: both timestamps, status, and `ModificationInfo` in modified mode.
  - An aggregate picker (4C, [#220]): the server's `HistoryServerCapabilities/AggregateFunctions` folder, browsed once per connection, and an interval; the read is `history` with `aggregate` and `interval`, forward, over the last intervals a page holds, with "load earlier" the range before.
  - Edit dialogs: insert, replace, update, delete range, delete at time.
  - A history tab on node-detail, shown when `historizing` ([`ReadAwait.cpp:25`](../../../apps/OpcGateway/src/async/ReadAwait.cpp#L25)) and `userAccessLevel`'s HistoryRead bit are set.
- **Live tail.** Join the read to the node's existing `/opc` subscription by source time, dropping duplicates. For a group source, Phase 6 replaces this with `HistSubscribe`'s snapshot and stream.
- **Where.** In `web/opc/control/src`, the current tree.

## Phase 5 — gateway historian, step 4 ([#221])

Split so that each PR is reviewable and most of 5a is useful before any history is stored.

- **5a, plumbing** ([#222]). Each item is independent.
  - A connection-state callback on `IDataChange` ([`MonitoringNodes.h:15`](../../../apps/OpcGateway/src/types/MonitoringNodes.h#L15)).
  - `CreateMonitoredItemsRequest` ([`CreateMonitoredItemsRequest.h:5`](../../../apps/OpcGateway/src/uatypes/CreateMonitoredItemsRequest.h#L5)) takes timestamps, sampling and queue size as parameters. ModifyMonitoredItems is new.
  - The collector's own clients and credential. `UAClient::GetClient` keys by connection and credential ([`UAClient.h:35`](../../../apps/OpcGateway/src/UAClient.h#L35)); the collector needs a key no web session can share.
  - `BrowsePathsToNodeIdResponse` ([`uaTypes.h:6`](../../../apps/OpcGateway/src/uatypes/uaTypes.h#L6), called at [`UAClient.cpp:1351`](../../../apps/OpcGateway/src/UAClient.cpp#L1351)) takes a start NodeId and follows `HierarchicalReferences`.
  - The `autoincrement` column flag in the sqlite dialect.
  - `Authorize::Test` with criteria.
- **5b, groups and templates** ([#223]).
  - The four tables, with `guid`, the `pkTable` foreign key on `server_connection_id`, and the unique (connection, node) index.
  - `access_resources` rows, and their QL.
  - Template resolution and `resolveHistory`.
  - Membership changes write `NodeAdded`/`NodeRemoved` and change monitored items.
  - Built 2026-10-11: the historian tables' edits as a custom mutation `GatewayQL` routes (`HistGroupMAwait`), not a QL hook, and the host that owns the library and one listener per group (`src/hist`), with the `hist` block in [`Opc.Gateway.jsonnet`](../../../apps/OpcGateway/config/Opc.Gateway.jsonnet), which [`Opc.Hub.jsonnet`](../../../apps/OpcHub/config/Opc.Hub.jsonnet) takes with the gateway block, and the collector's `credentials` in it. The listener's `SendDataChange` is left for 5c.
- **5c, collection, reads and edits** ([#224]). The collector feeding the library through the listener 5b made. Then the reads with `group`, then the edits, whose values need a type rule of their own: the type the historian stored for the node is the natural one, since a correction shouldn't need the server reachable (spec *Open questions*). Also the lock-disabled path answering with an error.
  - Built 2026-10-11: the listener's `SendDataChange` into the group, with the read after joining an item the collector's client already had, the rebuild's `NamespaceArray` check and the lost member asked for again (the reviews of [#222] and [#223] deferred both here); `history` with `group` ([`HistGroupReadQLAwait`](../../../apps/OpcGateway/src/ql/HistGroupReadQLAwait.h)) and the four edits with it ([`HistGroupEditQLAwait`](../../../apps/OpcGateway/src/ql/HistGroupEditQLAwait.h)), routed by `GatewayQL` and the hub's `HubQL` as custom queries and mutations, not a QL hook, the edits gated by `Authorize::TestEnforced` and typed by each node's `DataType`, which the collector reads once its item is made and each file's `NodeAdded` carries; and the differential test, [`HistCollectorTests`](../../../apps/OpcGateway/tests/HistCollectorTests.cpp). The `hist` block was 5b's.
- **5d, subscriptions** ([#225]). The snapshot under the lock, the read outside it, paging, live streaming and pushed modifications, on the gateway's listener and the hub's.
  - Built 2026-10-12: `Group::Subscribe` registers the subscriber and takes the snapshot under the group's locks, a copy of the buffer and each day and modifications file as served then, and the `Subscription` it returns pages that frozen view through the raw read, with bounds, outside them; values stream from `Finish` and modifications from the flush after their day's commit. `GatewaySocketSession::HistSubscribe` ([`HistSubscriber.h`](../../../apps/OpcGateway/src/hist/HistSubscriber.h)) serves them as messages of `/opc`, `HistSubscribe` and `HistUnsubscribe` in [`Opc.FromClient.proto`](../../../apps/OpcGateway/src/types/proto/Opc.FromClient.proto) and `HistSubscribeAck`, `HistSnapshot` and `HistRecords` in [`Opc.FromServer.proto`](../../../apps/OpcGateway/src/types/proto/Opc.FromServer.proto), so a client holds one socket for live values and history, on the gateway's listener and the hub's alike; a `/hist` socket of their own was built first and folded in the same day (spec *Open questions*). Tests: the library's `SubscribeTests`, the gateway's `HistSubscribeTests` over the embedded OpcServer, and the hub's `SocketTests.HistSubscribeReachesTheHistorian`.
- **Differential test.** A gateway group over OpcServer's historizing pump nodes stores the same values OpcServer stores itself (spec *Hosts*). With matching thresholds, a group read and a pass-through read of the same range should agree apart from sampling, which gives a cross-check neither host has alone. Connection drops in that setup exercise the gap markers.

## Phase 6 — web for gateway-only features, step 5 ([#226])

- Group and template admin. The template editor picks browse paths from the tree; resolution status shows unresolved members, with a retry that calls `resolveHistory`.
- Per-node threshold overrides.
- Group access through `access_resources`.
- The live trend for group sources switches to `HistSubscribe`'s snapshot and stream, which has no gap.
- Heartbeats are marked on the trend.
- Built 2026-10-12 (6a, [#335]): a group's history on node-detail's History tab, a source beside the server's own for the children a group holds, one source at a time with a toggle for several. Two gateway fields serve the gate ([`HistGroupInfoQLAwait.h`](../../../apps/OpcGateway/src/ql/HistGroupInfoQLAwait.h)): `historyGroups( opc, nodes )`, which groups on the connection hold the page's nodes, among those the caller may read, asked with the page; and `historyCapabilities( group )`, the historian's aggregates over `Hist::Aggregates` and whether the caller may edit the group, Update and Delete as *Authorization* gates the edits, asked when the tab opens on it. Reads, "load earlier", the Values|Modifications toggle, the aggregate picker and the edits take the source; a heartbeat is a ring on the trend; a group source has no live tail until 6b ([#336]).
- Built 2026-10-13 (6b, [#336]): a group source's live trend is `HistSubscribe`'s. `Gateway.histSubscribe` ([`gateway-service.ts`](../../../web/opc/control/src/lib/services/gateway-service.ts)) names the subscription by its request id, forgets it as it sends the unsubscribe and drops what comes under it after, a page already on the wire or the late ack (historian-live-trend #10), and delivers the ack, the snapshot's pages and the records as one stream, each record's node by its slot; an exception under the id ends it, and a dropped socket has it subscribe again, five seconds on and again on each failure, from the oldest of its nodes' latest source times delivered, at most ten minutes before the newest, so the snapshot fills the outage for a node whose source clock runs behind another's (historian-live-trend #5); an edit made during the outage to a record before that isn't delivered, since a modification is pushed and the snapshot is records. The History tab opens a group on the snapshot of the last ten minutes, from each node's bound, and "load earlier" is a reverse read ending where the snapshot began; a pushed modification is applied to the record it targets, so an edit on a live group needs no read after it; with Live off a group opens on a read, as a server does, and Live on again resumes from the values held, or opens again when none is inside the ten minutes (historian-live-trend #13). Two things the wire's `Timestamp` settles: a record the subscription delivers holds its source time as a `Date`, to the millisecond, so a row's Replace and Delete, which name the record to the tick, stay off until a read of it, as a push's do; and the ten-minute window stands in for a server's "last 1000 values", since a subscription starts from a time, not a count. The window, the Date's millisecond and the fixed retry were ruled as proposed on 2026-10-13.
- Built 2026-10-15 (6c, [#337]): the admin pages, on the gateway's page under Applications as two tabs beside Connections, **Templates** and **Groups** ([`gateway-hist-list.ts`](../../../web/opc/control/src/lib/pages/gateway-detail/hist-admin/gateway-hist-list.ts)), each a `ql-list` over the gateway's own table that the tab resolves itself, with a row opening `apps/gateways/<instance>/templates/<slug>` or `groups/<slug>` (`QLList.linkPrefix`; the bare `templates` and `groups` urls redirect to the page, so Back and Cancel land on it). The two pages are `DetailPage`s over `HistTemplate` and `HistGroup` ([`hist-admin.ts`](../../../web/opc/control/src/lib/model/hist-admin.ts)), resolved by [`hist-admin-resolver.ts`](../../../web/opc/control/src/lib/services/resolvers/hist-admin-resolver.ts) as a connection's is. A group's **Properties** holds its row, its source - the connection, the template or none, and the root, fixed once saved since `HistGroupMAwait` refuses the update - and its thresholds as five fields ([`hist-threshold-fields.ts`](../../../web/opc/control/src/lib/pages/gateway-detail/hist-admin/hist-threshold-fields.ts), the thresholds dialog's editor as a control the pages share); **Members** lists `histGroupNodes` with each node's template member and own columns, adds nodes picked on the connection and purges one, each its own mutation through [`hist-admin-service.ts`](../../../web/opc/control/src/lib/services/hist-admin-service.ts), and opens 6d's Thresholds dialog; **Resolution** shows the template's members against the rows and **Resolve** calls `resolveHistory`, which now answers the holder beside `Bad_NodeIdExists` (`holder{ id name }`, [`HistHost.h`](../../../apps/OpcGateway/src/hist/HistHost.h) `HistResolved`), and a `createHistGroupNode` for a node a group holds is refused naming that group to a caller with Read on it (by id to the rest), the index asked once it refuses the insert ([`HistGroupQLAwait.cpp`](../../../apps/OpcGateway/src/ql/HistGroupQLAwait.cpp)). A template's **Members** lists `histTemplateNodes` with the thresholds each sets, edits them inline, purges one, and adds paths from the picker; **Groups** links the groups made from it, the deleted ones too, marked, since a member's purge reaches them and the confirmation counts them. The picker ([`hist-node-picker-dialog.ts`](../../../web/opc/control/src/lib/pages/gateway-detail/hist-admin/hist-node-picker-dialog.ts)) is a page per node in a dialog, as the node pages browse, not a tree component: a trail leads back up, an object opens, a variable is ticked, and each pick is spelled for the tables - the node by URI and its path from the start as browse names by URI through the server's namespace array, so a namespace the server lists by no URI can't be picked; a template's picker lets the user choose the connection and the root, by text or by making the folder shown the root, since a template names no server; a group's starts at the group's root on its connection, and the root picker chooses an object. Two decisions ruled provisionally, for the user to confirm: the pages live on the gateway's page rather than under a route of their own, since groups and templates are the gateway's tables and every gateway list is there; and the picker browses a page at a time rather than as a tree.
- Built 2026-10-13 (6d, [#338]), ahead of 6c's pages: per-node threshold overrides from a **Thresholds** dialog on the History tab's group source ([`hist-thresholds-dialog.ts`](../../../web/opc/control/src/lib/pages/node-detail/node-history/hist-thresholds-dialog.ts)), which 6c's group detail can host as it stands. It lists the group's members with each threshold's effective value and its source - the row's own, its template member's, the group's or the default - and overrides or clears a column through `updateHistGroupNode`, which `HistGroupMAwait` applies to the running group before answering; the list reads again after each. A third gateway field serves it, `historyThresholds( group ){ update nodes{ id node templateNode path enumeration band thresholds{ name value source } } }` ([`HistGroupInfoQLAwait.h`](../../../apps/OpcGateway/src/ql/HistGroupInfoQLAwait.h)): each row's node by the server's index, so the page keys it as its variables, its template member and path, each threshold resolved by the row, its member and the group with where it comes from (`ResolveThresholdSources`, [`HistRows.h`](../../../apps/OpcGateway/src/hist/HistRows.h)), whether the server said the node is an enumeration - the collector's in-memory answer, the issue's open question, which the page shows as a band not applied - `band`, whether the deviation band is in force as the group holds it after `ForGroup` and the library's range check (`none`, `applied`, `enumeration`, `noRange` for a percent format the node has no usable range for, or `waiting` for the server to describe the node; `HistGroupHost::Band`, added by the review's #4), and `update`, whether the caller may update the rows, the table's right rather than the group's edit grant.
- Built 2026-10-15 (6e, [#339]): an **Access** tab on a saved group ([`hist-group-access.ts`](../../../web/opc/control/src/lib/pages/gateway-detail/hist-admin/hist-group-access.ts)), the node page's grid of roles against rights ([`node-rights`](../../../web/opc/control/src/lib/pages/node-detail/node-access/node-rights/node-rights.ts), its toggle and mutation logic now shared functions in [`node-access.ts`](../../../web/opc/control/src/lib/pages/node-detail/node-access/node-access.ts)) over Read, Update, Delete and Administer on the group's own `access_resources` row, `gateway`/`histGroups` with the group's guid as its criteria (its id until [#357], which also purges the row with the group), through the hub's access schema as the node tab grants. A grant saved on a group with no row of its own names it by key, so `access_role_add` mints it enforced and named for the group, as a node's first grant does; **Make own resource** is a `createResource`, which `ResourceMAwait` leaves unenforced, for grants to go on before it takes over; **Enforce** and **Stop enforcing** are the Resources page's switch, `restoreResource` and `deleteResource` on that row, confirmed both ways as there. A card above the grid says which row governs the group, from the table's `resources` rows, as `FindActiveResourcePKLocked` ([`Authorize.h`](../../../include/jde/access/Authorize.h)) resolves it: the `hist_groups` table's row is the slug's switch, consulted first - while it is unenforced no criteria row of the slug is looked at, whatever its own state, as `AuthorizeTests` asserts ("a deleted root disables its slug") - then the group's own row where it has one enforced, else the table's; so nothing governs a group until the table's row is enforced, and the card says so, with what a grant here does about it. Whether a group's own row should govern on its own, as a node's does under `OpcAuthorize`, is open for the user to rule; the tab's status is a one-line change either way. Grants on the table's row stay the role pages', its enforcement the Resources page's, which the tab links.

## Slices

Phases 1–4 are built in three passes, and each pass merges end to end before the next starts. Each pass is a GitHub milestone: [A](https://github.com/Jde-cpp/opc-hub/milestone/1), [B](https://github.com/Jde-cpp/opc-hub/milestone/2) and [C](https://github.com/Jde-cpp/opc-hub/milestone/3).

| Slice | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
| --- | --- | --- | --- | --- |
| **A**, raw reads | steps 1–4 ([#200]) | collection, `readRaw`, load, read capability ([#209]) | `history` with `opc` ([#214]) | trend, table, paging ([#218]) |
| **B**, edits | step 5 ([#206]) | `updateData`, `deleteRawModified`, `readModified`, `UAAccess` stubs ([#210]) | `createHistory` … `purgeHistory`, `modified` ([#215]) | edit dialogs, modified view ([#219]) |
| **C**, at-time and aggregates | steps 6–7 ([#207]) | `readAtTime`, `readProcessed`, `AggregateFunctions`, Median ([#211]) | `history` at times and aggregated ([#216]) | aggregate picker ([#220]) |

Slice A is the long one, since it carries all of storage. Its payoff is the check that matters most: the result shape, the continuation and the web components are proven against a real server before Phase 5 builds a second producer for them.

## Risks

- **Library shaped by its first host.** Mitigated by the host-interface table and a multi-group fixture from Phase 1.
- **The service lock.** OpcServer's history callbacks run on its one server thread. The one-day-per-callback bound is the guard; measure it under the emulator in Phase 2 rather than assume it.
- **Pass-through overfetch.** Merging nodes up to a horizon rereads what lies past it (spec *Pass-through*). A 10-node read that asks each node for `limit` values fetches up to 10× what it returns. An at-time or aggregate read, whose details have no per-node count, asks for at most the `limit` points from the page's first, which bounds it the same way. The first round's response then holds up to `nodes × limit` values, about 30 MB for 100 dense nodes at the default limit, which a server with a message limit of a few MB refuses whole, failing the call. Each read's merge also rescans what it holds every round: a raw read's pending values, and an at-time or aggregate read's points from the call's first. If that shows, ask for `limit / nodes` per node, rounded up, and accept more round trips when one node dominates.
- **Spec drift.** When implementation forces a change, the PR changes `spec.md` with it, so the spec stays the description of what ships.

[#193]: https://github.com/Jde-cpp/opc-hub/issues/193
[#194]: https://github.com/Jde-cpp/opc-hub/issues/194
[#195]: https://github.com/Jde-cpp/opc-hub/issues/195
[#196]: https://github.com/Jde-cpp/opc-hub/issues/196
[#197]: https://github.com/Jde-cpp/opc-hub/issues/197
[#198]: https://github.com/Jde-cpp/opc-hub/issues/198
[#199]: https://github.com/Jde-cpp/opc-hub/issues/199
[#200]: https://github.com/Jde-cpp/opc-hub/issues/200
[#201]: https://github.com/Jde-cpp/opc-hub/issues/201
[#202]: https://github.com/Jde-cpp/opc-hub/issues/202
[#205]: https://github.com/Jde-cpp/opc-hub/issues/205
[#206]: https://github.com/Jde-cpp/opc-hub/issues/206
[#207]: https://github.com/Jde-cpp/opc-hub/issues/207
[#208]: https://github.com/Jde-cpp/opc-hub/issues/208
[#209]: https://github.com/Jde-cpp/opc-hub/issues/209
[#210]: https://github.com/Jde-cpp/opc-hub/issues/210
[#211]: https://github.com/Jde-cpp/opc-hub/issues/211
[#212]: https://github.com/Jde-cpp/opc-hub/issues/212
[#213]: https://github.com/Jde-cpp/opc-hub/issues/213
[#214]: https://github.com/Jde-cpp/opc-hub/issues/214
[#215]: https://github.com/Jde-cpp/opc-hub/issues/215
[#216]: https://github.com/Jde-cpp/opc-hub/issues/216
[#217]: https://github.com/Jde-cpp/opc-hub/issues/217
[#218]: https://github.com/Jde-cpp/opc-hub/issues/218
[#219]: https://github.com/Jde-cpp/opc-hub/issues/219
[#220]: https://github.com/Jde-cpp/opc-hub/issues/220
[#221]: https://github.com/Jde-cpp/opc-hub/issues/221
[#222]: https://github.com/Jde-cpp/opc-hub/issues/222
[#223]: https://github.com/Jde-cpp/opc-hub/issues/223
[#224]: https://github.com/Jde-cpp/opc-hub/issues/224
[#225]: https://github.com/Jde-cpp/opc-hub/issues/225
[#226]: https://github.com/Jde-cpp/opc-hub/issues/226
[#335]: https://github.com/Jde-cpp/opc-hub/issues/335
[#336]: https://github.com/Jde-cpp/opc-hub/issues/336
[#338]: https://github.com/Jde-cpp/opc-hub/issues/338
[#339]: https://github.com/Jde-cpp/opc-hub/issues/339
[#357]: https://github.com/Jde-cpp/opc-hub/issues/357
[#237]: https://github.com/Jde-cpp/opc-hub/issues/237
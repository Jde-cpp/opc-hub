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
| node\_index | issued by the historian, carried in `FileStart` | the `hist_group_nodes` row id |
| Groups | one, `server` | many, named by `guid` |
| Break | stop or crash | also the connection-state callback |
| Identity | `OpcAuthorize` | `Authorize` through QL creds |
| Enqueue from | open62541's service lock | the connection's strand, under the monitoring lock |
| Clock and timers | injected | injected |

**Build order.** Each step merges with its own tests. Steps 1–4 are [#202]–[#205] under [#200], step 5 is [#206], and steps 6–7 are [#207].

1. **Records and I/O.** The `HistoryRecord` oneof, delimited writes straight into the buffer, a `CodedInputStream` reader over offset and limit, delta times, checkpoints, the first-open scan through the last good checkpoint, and the first append's truncation there. Tests truncate a live file at every byte offset, append a zero-filled tail, and garble a body with its length intact.
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
- **node\_index.** Carried forward in `FileStart`. A node the nodesets no longer historize gets a `NodeRemoved`.
- **Access.** Read callbacks check each node's Read. Implement the two `ASSERT(false)` stubs at [`UAAccess.cpp:566`](../../../apps/OpcServer/src/access/UAAccess.cpp#L566).
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
- **Dispatch.** `GatewayQLAwait` routes the `hist*` fields that carry `opc`. `needsClient` already opens the caller's client for any query with `opc` ([`GatewayQLAwait.cpp:45`](../../../apps/OpcGateway/src/ql/GatewayQLAwait.cpp#L45)), the same way `updateVariable` gets it ([`VariableQLAwait.cpp:11`](../../../apps/OpcGateway/src/ql/VariableQLAwait.cpp#L11)).
- **Shared code.** The continuation codec (time, per-node counts at that time, argument hash) and the QL result serializer live where Phase 5 reuses them, not in the pass-through.
- **Tests.** The gateway tests already embed OpcServer in-process ([`OpcGateway/tests/main.cpp:29`](../../../apps/OpcGateway/tests/main.cpp#L29)), so they read Phase 2's historizing pump nodes:
  - paging across day files, forward and reverse;
  - a two-node read where one node is dense and one sparse, which exercises the merge horizon;
  - a connection dropped between pages, whose continuation still resumes because it is by time;
  - every edit type, followed by a modified read.

## Phase 4 — web for pass-through, step 3 ([#217])

- **Charting library.** There is none in [`opc/control/package.json`](../../../web/opc/control/package.json) today, so choosing one comes first. It needs stepped and interpolated series, a time axis that pans into more pages, and point markers for status.
- **One history service** takes a source, `{opc}` or `{group}`. Phase 4 implements `opc`, and Phase 6 adds `group` without touching the components.
- **Components.**
  - A trend: stepped by default, gaps drawn where status is Bad; "load earlier" is a reverse read with only `end`.
  - A table: both timestamps, status, and `ModificationInfo` in modified mode.
  - Edit dialogs: insert, replace, update, delete range, delete at time.
  - A history tab on node-detail, shown when `historizing` ([`ReadAwait.cpp:25`](../../../apps/OpcGateway/src/async/ReadAwait.cpp#L25)) and `userAccessLevel`'s HistoryRead bit are set.
- **Live tail.** Join the read to the node's existing `/opc` subscription by source time, dropping duplicates. For a group source, Phase 6 replaces this with `/hist`.
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
  - Template resolution and `histResolve`.
  - Membership changes write `NodeAdded`/`NodeRemoved` and change monitored items.
- **5c, collection, reads and edits** ([#224]). A hist QL hook beside `OpcQLHook`, and the collector feeding the library. Then the reads with `group`, then the edits. Also the `hist` block in [`Opc.Gateway.jsonnet`](../../../apps/OpcGateway/config/Opc.Gateway.jsonnet) and [`Opc.Hub.jsonnet`](../../../apps/OpcHub/config/Opc.Hub.jsonnet), and the lock-disabled path answering with an error.
- **5d, `/hist`** ([#225]). The snapshot under the lock, the read outside it, paging, live streaming and pushed modifications, on the gateway's listener and the hub's.
- **Differential test.** A gateway group over OpcServer's historizing pump nodes stores the same values OpcServer stores itself (spec *Hosts*). With matching thresholds, a group read and a pass-through read of the same range should agree apart from sampling, which gives a cross-check neither host has alone. Connection drops in that setup exercise the gap markers.

## Phase 6 — web for gateway-only features, step 5 ([#226])

- Group and template admin. The template editor picks browse paths from the tree; resolution status shows unresolved members, with a retry that calls `histResolve`.
- Per-node threshold overrides.
- Group access through `access_resources`.
- The live trend for group sources switches to `/hist`'s snapshot and stream, which has no gap.
- Heartbeats are marked on the trend.

## Slices

Phases 1–4 are built in three passes, and each pass merges end to end before the next starts. Each pass is a GitHub milestone: [A](https://github.com/Jde-cpp/opc-hub/milestone/1), [B](https://github.com/Jde-cpp/opc-hub/milestone/2) and [C](https://github.com/Jde-cpp/opc-hub/milestone/3).

| Slice | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
| --- | --- | --- | --- | --- |
| **A**, raw reads | steps 1–4 ([#200]) | collection, `readRaw`, load, read capability ([#209]) | `hist` with `opc` ([#214]) | trend, table, paging ([#218]) |
| **B**, edits | step 5 ([#206]) | `updateData`, `deleteRawModified`, `readModified`, `UAAccess` stubs ([#210]) | `histInsert` … `histDeleteAtTime`, `modified` ([#215]) | edit dialogs, modified view ([#219]) |
| **C**, at-time and aggregates | steps 6–7 ([#207]) | `readAtTime`, `readProcessed`, `AggregateFunctions`, Median ([#211]) | `histAtTime`, `histAggregate` ([#216]) | aggregate picker ([#220]) |

Slice A is the long one, since it carries all of storage. Its payoff is the check that matters most: the result shape, the continuation and the web components are proven against a real server before Phase 5 builds a second producer for them.

## Risks

- **Library shaped by its first host.** Mitigated by the host-interface table and a multi-group fixture from Phase 1.
- **The service lock.** OpcServer's history callbacks run on its one server thread. The one-day-per-callback bound is the guard; measure it under the emulator in Phase 2 rather than assume it.
- **Pass-through overfetch.** Merging nodes up to a horizon rereads what lies past it (spec *Pass-through*). A 10-node read that asks each node for `limit` values fetches up to 10× what it returns. The first round's response then holds up to `nodes × limit` values, about 30 MB for 100 dense nodes at the default limit, which a server with a message limit of a few MB refuses whole, failing the call. The merge also rescans every pending value each round. If that shows, ask for `limit / nodes` per node, rounded up, and accept more round trips when one node dominates.
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
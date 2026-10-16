# Gateways

A **gateway** is a running OPC gateway service. It holds **server connections**, one per OPC UA server it talks to, and each connection exposes that server's address space as a tree of **nodes**.

## Browsing

The [Gateways](/gateways) page lists the gateways. From there:

- a gateway lists its connections
- a connection opens the root of its node tree

If the gateway cannot open a session for you, a message says why and you are returned to the gateway's page. The server may be down. Or it may want a sign-in of its own that this session does not carry; see [An OPC UA server of your own](/help/overview#an-opc-ua-server-of-your-own).

The connection's **Connection** tab under [Applications](/apps) shows the reason too.

A node page has a **Children** tab, a **History** tab when the server keeps the history of any of its children that you may read or a group of the gateway's historian holds one, and, for the bundled OPC server, a **Permissions** tab.

**Children** lists the node's child nodes with their current values. Click an object node to descend; the breadcrumb trail leads back up. The refresh button re-reads the values; the view buttons switch between saved views, and the tune button beside them sets a view's columns, filters and sort - see [Views](/help/lists#views).

**History** trends the node's historized children and lists their values. Where the server keeps its own history and the gateway keeps a group's too, a toggle picks the source, **Server** or the group by name, and the chips are the source's nodes. The chips pick the nodes, up to eight. The trend is stepped by default - a value holds until the next - or interpolated; drag to zoom, shift-drag to pan, and the strip under it scrolls. A `Bad` reading is a gap in its line, flagged `!` with its status; an `Uncertain` one keeps its value under a triangle. Opening reads the latest values; **Load earlier** reads the page before them, as does panning to the left edge. **Live** appends values as the server publishes them. On a group it is the gateway's own stream: the tab opens on the last ten minutes the historian stored, from each node's value before them, and appends each record as it is stored, so the trend matches the archive; an edit's change is redrawn where it lands, and a connection lost is taken up again once the gateway is back, with the values stored meanwhile; an edit made meanwhile shows once the tab opens again. Turned off and on again, Live takes up from the values held, or opens on the last ten minutes again when none of them is that recent. A ring on a group's line marks a heartbeat, a stored repeat of the value before it, so a flat line reads as a feed that is up. The table lists every value loaded, newest first, with both timestamps and the status.

**Aggregate** reads one of the aggregates the server lists - `Average`, `Minimum`, `Count` and the rest of its `AggregateFunctions` folder - in place of the values, one per node for each interval of the length set beside it, in seconds, minutes, hours or days; a length typed is read on Enter, or once the field is left. The intervals sit on the clock, so a one-hour interval runs from the top of each hour, and each value is stamped at the start of its interval; the trend holds it across the interval when stepped. Opening reads the latest intervals up to now, and **Load earlier** the intervals before; past a range with no data at all, it goes on from the latest value before it, until the history begins, and no range starts before 1970. A `Partial` status marks an interval the history did not cover whole, an `Interpolated` one a value derived from the records around it rather than read; an interval with no record is `BadNoData`, a gap in the trend with no flag. Nothing is published live - an aggregate is computed by the server on each read - and no row offers a replace or a delete, since an aggregate is not a record; the **Edit** menu still is, and the tab reads the aggregates again after an edit.

**Modifications** lists the edits made to the history instead of the values: for an insert the value put in, for anything else the value it replaced or deleted, each with the kind of edit, when it was made and by whom. When the server lets you write a node's history, or the gateway grants Update or Delete on a group through an enforced resource, **Edit** offers **Insert a value** at a time that holds none, **Replace a value** at a time that holds one, **Update a value**, which does either, **Delete a range** and **Delete at times**; a value's row has a replace and a delete of its own. The server applies its own rules and answers each value: the bundled OPC server leaves a range's end out, deletes the one value when the start equals the end, and refuses a delete at a time.

**Thresholds**, on a group, lists the group's members with each threshold's effective value - the deviation band and its format, the least and most time between stored values, and whether a value holds until the next - and where each comes from: the member's own row, its template member, the group, or the default. Where the gateway lets you update the rows, the edit button on a member overrides a column, or clears an override so the member inherits again; the gateway applies the change to the running group at once, and the list reads again so a cell shows what it resolved. An enumeration stores every change whatever its row says, so its deviation cells say `n/a`. A deviation in percent of the InstrumentRange or the EURange needs that range from the server: a member whose node has no usable one stores every change too, so its deviation cells are greyed and a warning names the missing range; changing the format or the deviation puts it right. Until the server has described a member, its band waits, and the deviation cell says so.

**Status** is the quality the server gives a value: **Good**, **Uncertain** or **Bad**, as OPC UA defines them. It shows by name, such as `Good`, `UncertainSensorNotAccurate` or `BadSensorFailure`.

A suffix adds what the server says about the reading itself:

- `+Low`, `+High` or `+Constant` when it is pinned at a limit
- `+Overflow` when readings were lost
- `+StructureChanged` or `+SemanticsChanged` when the node's definition has moved under it

Hover over the status for its numeric code.

A status that is not plain Good shows as an icon beside the value. The icon is an error mark for Bad, a warning for Uncertain, and an *i* for a Good that carries a note. Its tooltip gives the status.

The **Status** column is hidden in the default view. Switch it on in the view editor to read, sort or filter on it. The icon then moves into that column.

A **Bad** value is not a reading: the row keeps the last value it had, dimmed and locked against editing, until the server sends one that is not Bad. An Uncertain value is a reading, and is shown and edited as usual.

A value cell is also locked when the server reports the node read-only for the signed-in user - its access level carries no *CurrentWrite* right. A **lock** beside the cell says which it is, and its tooltip gives the reason and the rights the server did grant; switch on the **Access** column to see those rights for every row at once. Where the server reports no access level at all - some third-party servers do not - the cell stays editable and the server's own refusal, with its status code, is the answer.

A value cell reads *no read access* when the server will not let the signed-in user read the node. Its Status is blank. Its tooltip says to ask an administrator for a role that can read it.

**Permissions** shows who may read and write the node. It is offered for the bundled Jde OPC server only: that server enforces the hub's node rights itself, where a third-party server decides what its own users may do in its own configuration - see [What the hub gates](/help/access#what-the-hub-gates-and-what-it-does-not).

The address of a node is in the page's url, so a node page can be bookmarked or made a favorite.

The navbar search finds nodes by name in every connection this session has opened. The first search crawls a connection's tree once, to the depth and node limits under `gateway.search` in the gateway's settings. Later searches answer from memory. Inside a node tree only that connection is searched.

## Instances

Under [Applications](/apps), a gateway instance's page has five tabs:

- **Connections** lists its server connections. The **Status** column reads *Connected*, *Idle* or *Error*. Press **Add** to connect the gateway to another OPC server, or open a row to edit one.
- **Templates** and **Groups** are the gateway historian's: what it stores, and by what rules. See [Historian groups and templates](#historian-groups-and-templates).
- **Logs** shows the running service's log.
- **Log Settings** changes the running service's log levels.

A connection's page holds its **URL** and **Certificate URI** on the **Properties** tab, saved with **Save**. **Test Connection** has the gateway connect with the values on the form, saved or not, and the **Connection** tab shows the answer: the server's details, or *Not connected* and why - a wrong URL, a certificate the gateway does not trust, a server that does not trust the gateway's. On a saved connection it also retries one the page opened *Not connected*. The test saves nothing and opens no lasting session; a connection that passes still has to be saved. It does issue the certificate the gateway presents for the connection, if there is none yet - the one the saved connection then uses.

An OPC server instance's page has **Logs** and **Log Settings**.

## Historian groups and templates

The gateway keeps the history of the nodes in its **groups**. A group is on one connection, and holds either the nodes of a **template** applied to a root there, or a list of nodes of its own. A template is a named node set, as browse paths from a root - `Motor/Speed`, say - so one template makes a group for each pump, on whichever connection hosts it. Both are edited from the gateway's page under [Applications](/apps), on its **Templates** and **Groups** tabs; a group's history shows on the node pages' **History** tab, as above.

**Templates.** Press **Add** on the Templates tab, name the template and save it; its tabs then open. **Members** lists its paths with the thresholds each sets - a column left blank leaves a group's node to inherit the group's. **Add members** browses a connection's tree, a page per folder, from a root of your choosing: pick the connection, type the root or open a folder and press **Use as root**, then tick the variables. Each ticked variable's path from that root becomes a member, named by namespace URI rather than index, so the template applies on any server that hosts the same model. Adding a member resolves it in every group made from the template at once; removing one takes its node out of each, deleted groups included, so a group restored later comes back without it. The edit button on a member changes the thresholds it sets. **Groups** lists the groups made from it, the deleted ones marked.

**Groups.** Press **Add** on the Groups tab. A group's **Source** is fixed once it is saved: the connection, the template, or none for a node list of the group's own, and the root the template's paths start from - a NodeId typed, or browsed for with the folder button; blank is the Objects folder. To change the source, purge the group and make it again. Its **Thresholds** are the group's defaults, for members that set none; blank leaves the historian's own default. Once saved, **Members** lists the group's nodes, each with the template member it came from or *added by hand*, and the columns its own row sets. **Add nodes** picks nodes on the connection, from the root; a node already in another group on the connection is refused, and the refusal names that group, since a node belongs to one group per server. Removing a node takes it out of the group - its history stays on disk, and another group may take it. **Thresholds** opens the dialog described under the History tab: every member's effective values, where each comes from, and the overrides.

**Access**, on a saved group, says who may read its history and edit it. The card at the top says which resource governs the group. The `hist_groups` table's resource, switched on the [Resources](/access/resources) page, is the switch: until it is enforced no grant is checked for any group - anyone signed in reads the history and may press **Resolve**, which is configuration and passes as the tables' edits do, while every history edit (an insert, replacement, correction or purge) is refused, since those need a grant on an enforced resource - whatever a group's own row says. Once it is, a group answers to its own resource where it has one that is enforced, else to the table's grants. The grid grants roles **Read** (the trend, the table and a live subscription), **Update** (inserts, replacements, corrections and Resolve), **Delete** (purges) and **Administer** on the group's own resource; press **Save** to apply the ticks. While the table's resource governs the group, a role's **Inherited** box is ticked where it holds a grant on the table's resource, and its tooltip says which rights - what it holds on the group until the group's own resource takes over. Administer is what changing these grants takes once the group's own resource governs it, and no other page grants it there. A group with no resource of its own gets one from the first grant saved, enforced from that moment, so once the table's resource is enforced only the roles granted here get through, and only those with Administer change the grants - grant a role you hold Read and Administer in that first save, or the grants can't be changed again until **Stop enforcing**. **Make own resource** makes the row without enforcing it, so grants can go on before it takes over, and **Enforce** and **Stop enforcing** switch it, as the [Resources](/access/resources) page switches a table's; both ask first. Grants on the table's row, which apply to every group, are made on each role's page.

**Resolution**, on a group with a template, lists each of the template's members with the node it resolved to, or why it has none. **Resolve** retries the members without a node - a path the server did not have, a server that was down - and answers each: *Resolved*, a status such as `BadNoMatch` for a path the server does not have, or `BadNodeIdExists` naming the group that already holds the node, this one included when a member and a node added by hand resolve to the same node. A group you may not read is given by its number, not its name. Resolve needs Update on the group.

A deleted group keeps its rows, so its nodes stay held until it is restored or purged; a template a group uses cannot be purged.

## Security

A connection's **Certificate URI** decides how the gateway talks to the server.

Set it to the server's application URI and the gateway opens a **Sign & Encrypt** session under the strongest security policy the two share - Aes256_Sha256_RsaPss, Aes128_Sha256_RsaOaep or Basic256Sha256 - with a certificate it issues for that connection, and sends the user's credential encrypted.

Leave it empty and the gateway connects with **no security** (policy None). The data travels in the clear, and no certificate identifies the gateway to the server.

The user's credential is still encrypted. The gateway encrypts it to the server's certificate wherever the server asks for that, as the bundled OPC server and most others do. So a sign-in works on any server that publishes an unsecured endpoint at the connection's URL. Where it publishes none, the connection shows *Error* and says to set the URI.

The URI is the *Application URI* on the connection's **Connection** tab, or whatever the server's own configuration calls its URI. That tab shows the server's details, with the policy and mode the session negotiated, while the gateway holds a session with it or after a **Test Connection** that reached it; otherwise it says *Not connected* and why. So on a new connection:

1. Enter the slug and URL, leave the Certificate URI empty, and press **Test Connection**.
2. Read the *Application URI* on the **Connection** tab - or, from a server with no unsecured endpoint, in the error the tab shows, which names it.
3. Set the Certificate URI to it, test again, and save.

The credential is the user's: the session's sign-in token for the bundled OPC server, or the name and password entered for the connection. A password travels encrypted on a secured session, and on an unsecured one wherever the server's sign-in policy asks for encryption. A server that would take it only in the clear is refused, and the connection's *Error* says so; the gateway's `allowPlaintextPassword` setting permits it, for a server that can do no better on a network you trust.

Trust runs both ways, and each side keeps a directory of the certificates it trusts:

- **The gateway trusts the server.** Before a session opens, the gateway checks the server's certificate against its trusted-server directories (`gateway.trustedCertDirs` in its settings). They are the bundled OPC server's own, and `ssl/servers` under the gateway's data directory for every other server. Copy the server's certificate into `ssl/servers` and the next connection attempt picks it up. Any of `.pem`, `.crt`, `.der` or `.cer` works; most servers write `.der`. This list covers only the servers the gateway will talk to. The certificates that may sign in to the hub are a separate list. A connection in *Error* with "server certificate … rejected" names the server and the setting that lists the directories.
- **The server trusts the gateway.** The gateway's certificate for a connection is `ssl/certs/<instance>.<connection slug>.pem` under its data directory (`C:\ProgramData\Jde-Cpp\OpcHub` on Windows, `/var/lib/Jde-Cpp/OpcHub` on Linux) - `OpcHub.OpcServer.pem` for the bundled server, which trusts that directory as shipped. A third-party server rejects a client it does not know and lists it for an operator to trust in its own configuration tool; until then the connection shows *Error*, saying the server refused the secure channel and naming the certificate file it has to trust.

The certificate names the gateway, not the server: its application URI is `urn:<machine>:Jde-Cpp:<product>` (`urn:PLANT-PC:Jde-Cpp:OpcHub`, say), which is how the gateway introduces itself to every server and how it reads in a server's trust list. The connection's Certificate URI is the *server's*, and only selects its endpoints. A renamed machine re-issues these certificates under the new name, as does the upgrade from a version that labelled them with the server's URI; a third-party server then has the new file to trust.

The repository ships a PLC emulator (`apps/OpcServer/emulator`) that feeds the OPC server with changing values, for trying the pages out without plant equipment. Run with its `Opc.PlcEmulator.Quality` configuration, it also cycles the pumps through an example of each kind of status.
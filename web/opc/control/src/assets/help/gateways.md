# Gateways

A **gateway** is a running OPC gateway service. It holds **server connections**, one per OPC UA server it talks to, and each connection exposes that server's address space as a tree of **nodes**.

## Browsing

The [Gateways](/gateways) page lists the gateways. From there:

- a gateway lists its connections
- a connection opens the root of its node tree

If the gateway cannot open a session for you, a message says why and you are returned to the gateway's page. The server may be down. Or it may want a sign-in of its own that this session does not carry; see [An OPC UA server of your own](/help/overview#an-opc-ua-server-of-your-own).

The connection's **Connection** tab under [Applications](/apps) shows the reason too.

A node page has a **Children** tab, a **History** tab when the server keeps the history of any of its children that you may read, and, for the bundled OPC server, a **Permissions** tab.

**Children** lists the node's child nodes with their current values. Click an object node to descend; the breadcrumb trail leads back up. The refresh button re-reads the values; the view buttons switch between saved views, and the tune button beside them sets a view's columns, filters and sort - see [Views](/help/lists#views).

**History** trends the node's historized children and lists their values. The chips pick the nodes, up to eight. The trend is stepped by default - a value holds until the next - or interpolated; drag to zoom, shift-drag to pan, and the strip under it scrolls. A `Bad` reading is a gap in its line, flagged `!` with its status; an `Uncertain` one keeps its value under a triangle. Opening reads the latest values; **Load earlier** reads the page before them, as does panning to the left edge. **Live** appends values as the server publishes them. The table lists every value loaded, newest first, with both timestamps and the status.

**Aggregate** reads one of the aggregates the server lists - `Average`, `Minimum`, `Count` and the rest of its `AggregateFunctions` folder - in place of the values, one per node for each interval of the length set beside it, in seconds, minutes, hours or days. The intervals sit on the clock, so a one-hour interval runs from the top of each hour, and each value is stamped at the start of its interval; the trend holds it across the interval when stepped. Opening reads the latest intervals up to now, and **Load earlier** the intervals before, until a range holds no data at all. A `Partial` status marks an interval the history did not cover whole, an `Interpolated` one a value derived from the records around it rather than read; an interval with no record is `BadNoData`, a gap in the trend with no flag. Nothing is published live - an aggregate is computed by the server on each read - and no row offers a replace or a delete, since an aggregate is not a record; the **Edit** menu still is, and the tab reads the aggregates again after an edit.

**Modifications** lists the edits made to the history instead of the values: for an insert the value put in, for anything else the value it replaced or deleted, each with the kind of edit, when it was made and by whom. When the server lets you write a node's history, **Edit** offers **Insert a value** at a time that holds none, **Replace a value** at a time that holds one, **Update a value**, which does either, **Delete a range** and **Delete at times**; a value's row has a replace and a delete of its own. The server applies its own rules and answers each value: the bundled OPC server leaves a range's end out, deletes the one value when the start equals the end, and refuses a delete at a time.

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

Under [Applications](/apps), a gateway instance's page has three tabs:

- **Connections** lists its server connections. The **Status** column reads *Connected*, *Idle* or *Error*. Press **Add** to connect the gateway to another OPC server, or open a row to edit one.
- **Logs** shows the running service's log.
- **Log Settings** changes the running service's log levels.

A connection's page holds its **URL** and **Certificate URI** on the **Properties** tab, saved with **Save**. Once the connection is saved, its **Connection** tab shows the server's details.

An OPC server instance's page has **Logs** and **Log Settings**.

## Security

A connection's **Certificate URI** decides how the gateway talks to the server.

Set it to the server's application URI and the gateway opens a **Sign & Encrypt** session under the strongest security policy the two share - Aes256_Sha256_RsaPss, Aes128_Sha256_RsaOaep or Basic256Sha256 - with a certificate it issues for that connection, and sends the user's credential encrypted.

Leave it empty and the gateway connects with **no security** (policy None). The data travels in the clear, and no certificate identifies the gateway to the server.

The user's credential is still encrypted. The gateway encrypts it to the server's certificate wherever the server asks for that, as the bundled OPC server and most others do. So a sign-in works on any server that publishes an unsecured endpoint at the connection's URL. Where it publishes none, the connection shows *Error* and says to set the URI.

The URI is the *Application URI* on the connection's **Connection** tab, or whatever the server's own configuration calls its URI. That tab shows the server's details, with the policy and mode the session negotiated, only while the gateway holds a session with it; otherwise it says *Not connected* and why. So on a new connection:

1. Save it with the Certificate URI empty.
2. Read the *Application URI* on its **Connection** tab - or, from a server with no unsecured endpoint, in the error the connection shows, which names it.
3. Set the Certificate URI to it and save again.

The credential is the user's: the session's sign-in token for the bundled OPC server, or the name and password entered for the connection. A password travels encrypted on a secured session, and on an unsecured one wherever the server's sign-in policy asks for encryption. A server that would take it only in the clear is refused, and the connection's *Error* says so; the gateway's `allowPlaintextPassword` setting permits it, for a server that can do no better on a network you trust.

Trust runs both ways, and each side keeps a directory of the certificates it trusts:

- **The gateway trusts the server.** Before a session opens, the gateway checks the server's certificate against its trusted-server directories (`gateway.trustedCertDirs` in its settings). They are the bundled OPC server's own, and `ssl/servers` under the gateway's data directory for every other server. Copy the server's certificate into `ssl/servers` and the next connection attempt picks it up. Any of `.pem`, `.crt`, `.der` or `.cer` works; most servers write `.der`. This list covers only the servers the gateway will talk to. The certificates that may sign in to the hub are a separate list. A connection in *Error* with "server certificate … rejected" names the server and the setting that lists the directories.
- **The server trusts the gateway.** The gateway's certificate for a connection is `ssl/certs/<instance>.<connection slug>.pem` under its data directory (`C:\ProgramData\Jde-Cpp\OpcHub` on Windows, `/var/lib/Jde-Cpp/OpcHub` on Linux) - `OpcHub.OpcServer.pem` for the bundled server, which trusts that directory as shipped. A third-party server rejects a client it does not know and lists it for an operator to trust in its own configuration tool; until then the connection shows *Error*, saying the server refused the secure channel and naming the certificate file it has to trust.

The certificate names the gateway, not the server: its application URI is `urn:<machine>:Jde-Cpp:<product>` (`urn:PLANT-PC:Jde-Cpp:OpcHub`, say), which is how the gateway introduces itself to every server and how it reads in a server's trust list. The connection's Certificate URI is the *server's*, and only selects its endpoints. A renamed machine re-issues these certificates under the new name, as does the upgrade from a version that labelled them with the server's URI; a third-party server then has the new file to trust.

The repository ships a PLC emulator (`apps/OpcServer/emulator`) that feeds the OPC server with changing values, for trying the pages out without plant equipment. Run with its `Opc.PlcEmulator.Quality` configuration, it also cycles the pumps through an example of each kind of status.

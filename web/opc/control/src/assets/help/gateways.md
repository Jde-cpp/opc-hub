# Gateways

A **gateway** is a running OPC gateway service. It holds **server connections**, one per OPC UA server it talks to, and each connection exposes that server's address space as a tree of **nodes**.

## Browsing

The [Gateways](/gateways) page lists the gateways; a gateway lists its connections; a connection opens the root of its node tree.

A node page has a **Children** tab and, for the bundled OPC server, a **Permissions** tab.

**Children** lists the node's child nodes with their current values. Click an object node to descend; the breadcrumb trail leads back up. The refresh button re-reads the values, the view buttons choose the columns, filters and sort - see [Views](/help/lists#views).

**Status** is the quality the server gives a value - **Good**, **Uncertain** or **Bad**, as OPC UA defines them - by its name: `Good`, `UncertainSensorNotAccurate`, `BadSensorFailure`. A suffix adds what the server says about the reading itself: `+Low`, `+High` or `+Constant` when it is pinned at a limit, `+Overflow` when readings were lost, `+StructureChanged` or `+SemanticsChanged` when the node's definition has moved under it. Hover over the status for its numeric code. A status that is not plain Good shows as an icon beside the value - an error mark for Bad, a warning for Uncertain, an *i* for a Good that carries a note - with the status in its tooltip. The **Status** column itself is hidden in the default view; switch it on in the view editor to read, sort or filter on it, and the icon moves into that column.

A **Bad** value is not a reading: the row keeps the last value it had, dimmed and locked against editing, until the server sends one that is not Bad. An Uncertain value is a reading, and is shown and edited as usual.

A value cell is also locked when the server reports the node read-only for the signed-in user - its access level carries no *CurrentWrite* right. A **lock** beside the cell says which it is, and its tooltip gives the reason and the rights the server did grant; switch on the **Access** column to see those rights for every row at once. Where the server reports no access level at all - some third-party servers do not - the cell stays editable and the server's own refusal, with its status code, is the answer.

**Permissions** shows who may read and write the node. It is offered for the bundled Jde OPC server only: that server enforces the hub's node rights itself, where a third-party server decides what its own users may do in its own configuration - see [What the hub gates](/help/access#what-the-hub-gates-and-what-it-does-not).

The address of a node is in the page's url, so a node page can be bookmarked or made a favorite. Nodes that have been browsed in this session also turn up in the navbar search.

## Instances

Under [Applications](/apps), a gateway instance's page has three tabs: its **Connections**, its **Logs**, and its **Log Settings**, which change the running service's log levels. An OPC server instance's page has **Logs** and **Log Settings**.

## Security

A connection's **Certificate URI** decides how the gateway talks to the server. Set it to the server's application URI (the *Application URI* on the connection's **Connection** tab, or whatever the server's own configuration calls its URI) and the gateway opens a **Sign & Encrypt** session under the strongest security policy the two share - Aes256_Sha256_RsaPss, Aes128_Sha256_RsaOaep or Basic256Sha256 - with a certificate it issues for that connection, and sends the user's credential encrypted. Leave it empty and the gateway connects with **no security** (policy None): the data travels in the clear and no certificate identifies the gateway to the server. The user's credential is still encrypted - the gateway encrypts it to the server's certificate wherever the server asks for that, as the bundled OPC server and most others do - so a sign-in works on any server that publishes an unsecured endpoint at the connection's URL; where it does not, the connection shows *Error* and says to set the URI. The **Connection** tab shows the policy and mode a live session negotiated - it shows the server's details only while the gateway holds a session with it, so on a new connection save it with the URI empty first, read the *Application URI* there - or, from a server with no unsecured endpoint, in the error the connection shows, which names it - then set it.

The credential is the user's: the session's sign-in token for the bundled OPC server, or the name and password entered for the connection. A password travels encrypted on a secured session, and on an unsecured one wherever the server's sign-in policy asks for encryption. A server that would take it only in the clear is refused, and the connection's *Error* says so; the gateway's `allowPlaintextPassword` setting permits it, for a server that can do no better on a network you trust.

Trust runs both ways, and each side keeps a directory of the certificates it trusts:

- **The gateway trusts the server.** Before a session opens, the gateway checks the server's certificate against its trusted-server directories (`gateway.trustedCertDirs` in its settings): the bundled OPC server's own, and `ssl/servers` under the gateway's data directory, which is there for every other server. Copy the server's certificate into `ssl/servers` - `.pem`, `.crt`, `.der` or `.cer`, whichever the server publishes (most write `.der`) - and the next connection attempt picks it up. These are only the servers the gateway will talk to - the certificates that may sign in to the hub are a separate list. A connection in *Error* with "server certificate … rejected" names the server and the directory to use.
- **The server trusts the gateway.** The gateway's certificate for a connection is `ssl/certs/<instance>.<connection slug>.pem` under its data directory (`C:\ProgramData\Jde-Cpp\OpcHub` on Windows, `/var/lib/Jde-Cpp/OpcHub` on Linux) - `OpcHub.OpcServer.pem` for the bundled server, which trusts that directory as shipped. The certificate names the gateway, not the server: its application URI is `urn:<machine>:Jde-Cpp:<product>` (`urn:PLANT-PC:Jde-Cpp:OpcHub`, say), which is how the gateway introduces itself to every server and how it reads in a server's trust list - the connection's Certificate URI is the *server's*, and only selects its endpoints. A third-party server rejects a client it does not know and lists it for an operator to trust in its own configuration tool; until then the connection shows *Error*, saying the server refused the secure channel and naming the certificate file it has to trust. A renamed machine re-issues these certificates under the new name, as does the upgrade from a version that labelled them with the server's URI; a third-party server then has the new file to trust.

The repository ships a PLC emulator (`apps/OpcServer/emulator`) that feeds the OPC server with changing values, for trying the pages out without plant equipment. Run with its `Opc.PlcEmulator.Quality` configuration, it also cycles the pumps through an example of each kind of status.

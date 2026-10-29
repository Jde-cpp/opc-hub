# Historian groups and templates

The gateway keeps the history of the nodes in its **groups**. A group is on one connection, and holds either the nodes of a **template** applied to a root there, or a list of nodes of its own.

A template is a named node set, as browse paths from a root - `Motor/Speed`, say - so one template makes a group for each pump, on whichever connection hosts it.

Both are edited from the gateway's page under [Applications](/apps), on its **Templates** and **Groups** tabs. A group's history shows on the node pages' **History** tab, as [Browsing](/help/gateways#browsing) describes.

## Templates

Press **Add** on the Templates tab, name the template and save it; its tabs then open.

- **Members** lists its paths with the thresholds each sets - a column left blank leaves a group's node to inherit the group's. The edit button on a member changes the thresholds it sets.
- **Add members** browses a connection's tree, a page per folder, from a root of your choosing: pick the connection, type the root or open a folder and press **Use as root**, then tick the variables. Each ticked variable's path from that root becomes a member, named by namespace URI rather than index, so the template applies on any server that hosts the same model.
- **Groups** lists the groups made from it, the deleted ones marked.

Adding a member resolves it in every group made from the template at once. Removing one takes its node out of each, deleted groups included, so a group restored later comes back without it.

## Groups

Press **Add** on the Groups tab. A group's **Properties** tab holds, under its name:

- **Source** - the connection, the template, or none for a node list of the group's own, and the root the template's paths start from: a NodeId typed, or browsed for with the folder button; blank is the Objects folder. A saved group shows its root by its browse path, with its NodeId under it.
- **Endpoint** - where the gateway's own OPC UA endpoint serves the group's members, with the group's history behind them: the endpoint's URL and the folder the members share under `Objects/<connection slug>`, each member at its path on the source. Or that it is not, and why: the gateway has no endpoint, the group is deleted, or the endpoint doesn't hold it because the historian isn't running.
- **Thresholds** - the group's defaults, for members that set none; blank leaves the historian's own default. Hover a field for what it does.
- **Guid** - the group's fixed id, which names its history files and its own resource; the button beside it copies it.

The source is fixed once the group is saved. To change it, purge the group and make it again.

### Members

Once the group is saved, **Members** lists its nodes, each with:

- its name and the type the gateway serves it with, as its server gives them - read once the collector has the node's item, so a node just added says *unread* until then. Behind the name are its path, where the endpoint serves it with its NodeId there, and its description.
- the template member it came from, or *added by hand*
- the threshold columns its own row sets

**Add nodes** picks nodes on the connection, from the root. A node already in another group on the connection is refused, and the refusal names that group, since a node belongs to one group per server.

Removing a node takes it out of the group. Its history stays on disk, and another group may take it.

**Thresholds** opens the dialog [Browsing](/help/gateways#browsing) describes under the History tab: every member's effective values, where each comes from, and the overrides.

## Access

**Access**, on a saved group, says who may read its history and edit it. The card at the top says which resource governs the group.

The `hist_groups` table's resource, switched on the [Resources](/access/resources) page, is the switch:

- Until it is enforced, no grant is checked for any group. Anyone signed in reads the history and may press **Resolve**, which is configuration and passes as the tables' edits do. Every history edit - an insert, replacement, correction or purge - is refused, since those need a grant on an enforced resource, whatever a group's own row says.
- Once it is, a group answers to its own resource where it has one that is enforced, else to the table's grants.

The grid grants roles **Read** (the trend, the table and a live subscription), **Update** (inserts, replacements, corrections and Resolve), **Delete** (purges) and **Administer** on the group's own resource; press **Save** to apply the ticks. While the table's resource governs the group, a role's **Inherited** box is ticked where it holds a grant on the table's resource, and its tooltip says which rights - what it holds on the group until the group's own resource takes over.

Administer is what changing these grants takes once the group's own resource governs it, and no other page grants it there. A group with no resource of its own gets one from the first grant saved, enforced from that moment. So once the table's resource is enforced, only the roles granted here get through, and only those with Administer change the grants - grant a role you hold Read and Administer in that first save, or the grants can't be changed again until **Stop enforcing**.

**Make own resource** makes the row without enforcing it, so grants can go on before it takes over. **Enforce** and **Stop enforcing** switch it, as the [Resources](/access/resources) page switches a table's; both ask first. Grants on the table's row, which apply to every group, are made on each role's page.

## Resolution

**Resolution**, on a group with a template, lists each of the template's members with the node it resolved to, or why it has none.

**Resolve** retries the members without a node - a path the server did not have, a server that was down - and answers each:

- *Resolved*
- a status such as `BadNoMatch`, for a path the server does not have
- `BadNodeIdExists`, naming the group that already holds the node - this one included, when a member and a node added by hand resolve to the same node

A group you may not read is given by its number, not its name. Resolve needs Update on the group.

## Deleted groups

A deleted group keeps its rows, so its nodes stay held until it is restored or purged. A template a group uses cannot be purged.
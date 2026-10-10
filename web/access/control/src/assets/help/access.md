# Access

The [Access](/access) section is where users are given rights.

A right comes from a **permission**: an allow or a deny of one action on one **resource**.

Permissions are collected into **roles**. Roles are granted to **groups** and to **users**.

These pages grant rights through roles only. A permission is never written straight onto a user or a group.

A deny always wins. What a user ends up with on a resource is everything the user's roles allow, less anything any of them denies.

The section opens on a card for each list: **Users**, **Groups**, **Roles** and **Resources**.

## Lists

The four lists work as every list on the site does - **Add**, the views, **Show deleted** - and so do the pages their rows open: see [Lists and records](/help/lists).

Two lists have no **Add**:

- [Users](/access/users) come into being by signing in. A Google or OPC server account is added the first time it signs in, and a certificate identity when the app server enrolls it.
- [Resources](/access/resources) are registered by the services themselves.

Resource rows do not open, and the list has no **Show deleted**. The **Enforced** switch in each row takes its place. See [Resources](#resources).

Users, groups and roles are deleted and restored as any record is. A deleted group or role also offers **Purge**, which removes it for good.

## Users

A user's page has four tabs: **Properties**, the **Groups** the user belongs to, the **Roles** granted to the user directly, and **Effective rights**.

**Properties** describes the account. A user that signs in with a certificate gets the key instead - subject, issuer, fingerprint and modulus, each with a copy button. The provider and the login name are fixed once the user exists.

**Effective rights** is what the user can actually do on each resource through every group and role, the same answer the server enforces. A tick is an allowed right and a red mark a denied one; hover either to read the grants behind it - a role, a role via a group, or a direct grant, which these pages do not create but the server may still hold. The **Enforced** column says whether the resource is checked at all.

A resource that is enforced and that the user holds no grant on is a lockout: every right it offers is marked, and the mark says so. That is the row to look for after turning **Enforced** on.

Rights granted on individual OPC nodes are folded under the row of the resource they belong to. Expand it to see them. Each is named, and links to its node page where the node can be resolved.

A node is enforced from the moment any role is granted on it. The OPC server checks that node and everything under it against the node's own grants alone, whatever the user holds on the table above it.

So a user with no grant on the node is locked out of that part of the tree, and the node's row shows the lockout.

When the list has no row for the node's table (the table is not enforced and the user holds no grant on it), the node row stands on its own in the list instead of under the table.

## Groups

A group's page has **Properties**, its **Users**, the child **Groups** nested in it and the **Roles** granted to it. A user in a group holds everything that group's roles carry, and everything carried by the groups it is nested in.

## Roles

A role's page has **Properties**, the **Permissions** it carries, the child **Roles** it includes, and the **Groups** and **Users** it is granted to. A role hands on everything its child roles carry.

Unchecking a child role cuts that one link. The child role itself, its permissions and its other memberships are left alone.

### The Permissions grid

One row per resource, one column per right, and a cell is clicked through three states:

1. blank - the role says nothing about that right
2. ticked - the role **allows** it
3. a red mark - the role **denies** it

Clicking a red mark returns the cell to blank. **All** runs a whole row through the same three states at once, and **None** clears everything the row allows. A right the resource does not offer has no cell at all. The read-only **Deleted** column is the resource's enforcement state: ticked means the resource is not enforced.

## Rights

| Right | On a record type | On an OPC node |
|---|---|---|
| **Create** | add a record | |
| **Read** | see records | read the value and its history |
| **Update** | change a record | write the value and its history |
| **Delete** | mark a record deleted | write history |
| **Purge** | remove a deleted record for good | |
| **Administer** | grant and revoke permissions on that resource | write the node's status, timestamp and access attributes |

**Subscribe** and **Execute** belong to the same vocabulary and appear only on the resources that declare them.

Most record types offer the six above, but not all:

- users add **Execute** and **Subscribe**;
- roles add **Subscribe**;
- the resources table offers only **Delete** and **Subscribe**;
- the `acl` table offers only **Read**, **Administer** and **Subscribe**.

## Resources

A resource is what a permission names. When a service starts it registers one for each table that offers rights, plus any it declares by name. Internal tables with no rights, such as `identities` and `providers`, get none.

Granting a role on an OPC node mints a resource scoped to that node.

The [Resources](/access/resources) list is the registered ones, a row each, with an **Enforced** toggle. Node-scoped resources are not listed here - they are granted from the node's own page, see [Gateways](/help/gateways), and appear under a user's **Effective rights**.

A new installation enforces nothing. While a resource is unenforced the hub checks nothing on it, and every signed-in user has full access to it. The OPC server still applies its own limits on what the signed-in user may read and write.

One exception: the bundled OPC server refuses history edits and deletes on its nodes until a node resource is enforced.

Once a resource is enforced, only a user holding a grant gets through.

### What the hub gates, and what it does not

The hub gates **its own** resources:

- the tables behind these pages;
- the server connection list, both reading it and opening a session on one of its connections.

Log settings are not gated by a role. Any signed-in user can read and change them.

The hub also gates the nodes of the **bundled** Jde OPC server, which enforces them itself. Delegation only lets that server answer who may grant rights on its nodes.

The hub does **not** gate the nodes of a third-party OPC server. You sign in to such a server as one of *its* users (`slug\user`), the hub opens the session as that user, and what you may then read or write is that server's own decision, taken in its own configuration tool. No role here grants or withholds it, and the node page's **Permissions** tab is not offered for one.

So a role's rights on an OPC node - the right-hand column above - describe the bundled server. Against a server of your own, a role still decides what a user may do *in the hub*; the values themselves are the server's to protect.

So grant first and enforce second. Enforcing a resource you hold no permission on shuts you out of it as surely as anyone else, which is why the toggle asks before it changes either way.

# Overview

OPC Hub is the web front end of the Jde OPC services: it browses and secures the OPC UA servers the gateways connect to, manages who may do what, and watches the services that make it up.

## Sections

- [Gateways](/gateways) - the gateways, their server connections and the node trees behind them. See [Gateways help](/help/gateways).
- [Access](/access) - users, groups, roles, resources and the permissions that tie them together. See [Access help](/help/access).
- [Applications](/apps) - the running services, their logs and their log levels. See [Applications help](/help/apps).

## First steps

### The bundled OPC server

1. Sign in with Google, using the account button at the right of the top bar.
2. There is nothing to add: the installer's OPC UA Server component set up the **OpcServer** connection (listed as *Jde OpcServer*, `opc.tcp://127.0.0.1:4840`), and your sign-in reaches it.

### An OPC UA server of your own

1. Under [Applications](/apps), open the **OpcHub** card, press **Add** on its **Connections** tab, fill these in and save:
   - **Slug** - the short name that stands for the connection in URLs and at sign-in, `plant1` say. It is fixed once saved.
   - **Name** - what the pages call the connection.
   - **Description** - optional, your own note of what the server is.
   - **URL** - the server's endpoint, `opc.tcp://plant-server:4840` say.
   - **Certificate URI** - empty for an unsecured session, or set as [Security](/help/gateways#security) describes.
   - **Default Namespace** - the namespace index a node address is taken to mean when it names none. Node urls then carry
     an index only where they differ from it (`4~Examples`), which is what keeps the common case readable. Set it to the
     index your server keeps its own nodes under - its documentation or its address space will say. Getting it wrong is
     not an error here; it shows up later as node addresses that will not resolve.
2. Have the hub and the server trust each other's certificates, as [Security](/help/gateways#security) describes.
3. Sign in as one of that server's users, on the [login page](/login):
   - **Username** - the connection's slug from step 1, a backslash and the user's name on the server: `plant1\operator1`.
     The slug prefix is what picks the connection, so give it. Without one the hub tries its default connection - the
     bundled server, when that component is installed - and otherwise refuses the sign-in with *"No default OPC server
     connection."* The bundled server is a connection like any other, and its slug is `OpcServer`.
   - **Password** - that user's password on the server.

   Adding the connection is what created this login: the hub makes a sign-in provider for the slug as the connection
   saves, and your first sign-in creates your user.

### Then

1. Open [Gateways](/gateways), pick the gateway, then the server connection, and browse the node tree.
2. Under [Access](/access), put users into groups and grant the groups roles. The installer seeds a set of roles to start from - Viewer, Operator, Engineer, Owner and others.
3. Only then turn on a resource's **Enforced** switch, on the [Resources](/access/resources) list. Until a resource is enforced the hub checks nothing on it and everyone gets through: the only restrictions are the OPC server's own, on what it lets the signed-in user read and write. Once it is enforced, only users holding a grant get through - yourself included, so grant first and enforce second.

## Getting around

The [home page](/) has a tile for each section and a row of the pages you visited recently. Search, favorites, breadcrumbs, themes and signing in are described under [Navigation](/help/navigation). The `?` button in the top bar opens the help topic for whichever page you are on.

## Services

| Service | Process | Role |
|---|---|---|
| OPC Hub | `Jde.Opc.Hub` | signs users in, holds the access database, collects the services' logs, connects to OPC UA servers and serves their nodes to this site |
| OPC server | `Jde.Opc.Server` | an OPC UA server of its own, fed by a PLC or by the emulator |

The hub is two services in one process, which is how the installer sets them up. They can also run apart: the **application server** (`Jde.App.Server`) signs users in, holds the access database and accumulates logs, and separate **OPC gateways** (`Jde.Opc.Gateway`), each holding the connections to its own OPC UA servers, can be set up for individual locations. The pages are the same either way.

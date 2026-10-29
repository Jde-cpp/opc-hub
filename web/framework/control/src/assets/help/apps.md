# Applications

The [Applications](/apps) page shows every service that is registered with the application server. Each card shows:

- the program
- the instance name
- how long it has been up
- its memory
- the host it runs on

Open a card for the instance's own page.

Which cards there are depends on how the services are laid out.

## Stand-alone

The installer's layout: the application server and the gateway run as one process, the hub, which registers once.

### OpcHub

The **OpcHub** card comes first. It is the process this site is served by. Its page has seven tabs: **Connections**, the OPC UA servers the hub talks to, **Templates** and **Groups**, its historian's, and **Endpoint**, its own OPC UA endpoint and the sessions on it, described under [Gateways](/help/gateways) and [Historian groups and templates](/help/gateways/groups), then [Logs](#logs), [Log Settings](#log-settings) and [Web Sessions](#web-sessions). There is no application server card beside it: the hub's log and log levels are the application server's as well.

## Distributed

The application server and the gateways run apart, and each registers on its own.

### Application server

An application server instance's page has three tabs, [Logs](#logs), [Log Settings](#log-settings) and [Web Sessions](#web-sessions).

### Gateways

A gateway instance's page has the [hub's six tabs](#opchub).

### OPC servers

An OPC server instance's page has the [application server's three tabs](#application-server).

The installer can add an OPC server of its own. On Windows it is the optional **OPC UA Server** component, off by default. On Linux the `.deb` ships it but leaves it stopped until `sudo systemctl enable --now jde-opcserver`, and the tarball's `install.sh` starts it only with `--opcserver`. Once it runs, its card shows beside the hub's.

A **PLC Emulator** card shows the same uptime, memory and host as the others. It is marked *No details page*, because there is no page to open.

## Logs

The entries this instance has logged, newest first, a page at a time. There is no narrowing by application, date or text.

The hub's log also holds the lines each connected application sends it. You see only those of applications whose log you may read: the `logs` resource on the Resources page, once it is enforced.

The toolbar holds:

- the *Level* filter, which hides everything below the level picked
- the refresh button, which reloads from the top
- the view buttons, which switch between saved views
- the tune button, which chooses the columns shown and their sort, and saves that as a view
- on the right, three buttons that act on the row you select by clicking it, or with Enter:
  - the hide button hides every entry with the selected entry's message
  - the next button goes to the next entry with that message, reading on through the log for one
  - the show button brings back every message you hid

The messages you hide stay hidden the next time you open a log, until you show them again.

See [Views](/help/lists#views) for more on views.

In the table, column headers sort as well, all but *Line*. The *Time* column shows the time of day; hover it for the full date, which matters once paging reaches an earlier day's entries.

The pager under the table turns the pages; the view's **Page Size** sets how many entries each holds, 21 unless the view says otherwise.

## Log Settings

The log levels the instance is running with, by tag. Rows marked *Configured* come from the instance's own configuration; rows marked *Override* were saved for this instance from this page and replace the configured level until they are removed.

Each log the instance writes has its own tab:

- **Binary**
- **Remote**, the log sent to the application server
- **Text**

A tab for a log the instance does not run says so. Its rows are saved but apply to nothing.

Changing a configured row's level makes it an override, and the undo button reverts it. The *Default* row's undo button removes a saved default override too: the row reads *Removed on save*, and after the save it shows the level the configuration gives.

To set a level for a tag that is not listed, pick the tag in the blank last row, then its level. The delete button on such a row removes the override, and the tag falls back to its configured level or the default.

Changes wait until you press **Save**; **Cancel** drops them and reloads what is saved. A saved change is pushed to the running service at once and kept, so the service applies it again at every start.

An instance that is not connected cannot report what it runs with, so only its saved overrides are listed; a change made then is picked up when the service next starts.

## Web Sessions

The web sessions the instance holds: one for each signed-in browser or service, and one for each address that has called it without signing in. A row shows the user, or *anonymous*, the address the session came from, when it last made a request and when it expires if it makes no more. The refresh button reloads the list.

Every page asks the application server, which answers for its own instance and asks a stand-alone gateway or an OPC server over the socket it registered on; an instance that is not connected cannot be listed.

Once the `app/sessions` resource is enforced, listing needs **Read** on it. It is not the gateway's `gateway/sessions`, which gates the gateway's OPC sessions.

A session cannot be ended from this page. The id that ends one is the session's own credential, which the page never shows.
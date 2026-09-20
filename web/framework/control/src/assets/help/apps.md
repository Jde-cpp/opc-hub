# Applications

The [Applications](/apps) page shows every service that is registered with the application server: the program, the instance name, how long it has been up, its memory and the host it runs on. Open a card for the instance's own page. Which cards there are depends on how the services are laid out.

## Stand-alone

The installer's layout: the application server and the gateway run as one process, the hub, which registers once.

### OpcHub

The **OpcHub** card comes first - it is the process this site is served by. Its page has three tabs: **Connections**, the OPC UA servers the hub talks to, described under [Gateways](/help/gateways), then [Logs](#logs) and [Log Settings](#log-settings). There is no application server card beside it: the hub's log and log levels are the application server's as well.

## Distributed

The application server and the gateways run apart, and each registers on its own.

### Application server

An application server instance's page has two tabs, [Logs](#logs) and [Log Settings](#log-settings).

### Gateways

A gateway instance's page has the hub's three tabs: **Connections**, described under [Gateways](/help/gateways), then [Logs](#logs) and [Log Settings](#log-settings).

### OPC servers

An OPC server instance's page has [Logs](#logs) and [Log Settings](#log-settings). The installer's bundled OPC server is one of these, so a stand-alone install shows its card beside the hub's.

A **PLC Emulator** card says the emulator is running and no more - it has no page to open.

## Logs

The entries this instance has sent, newest first, a page at a time. There is no narrowing by application, date or text: the page is one instance's log.

In the toolbar, the *Level* filter hides everything below the level picked; the refresh button reloads from the top; the view buttons switch between saved views, and the tune button beside them chooses which columns are shown and how they are sorted, and saves that as a view - see [Views](/help/lists#views).

In the table, column headers sort as well, all but *Line*. The *Time* column shows the time of day; hover it for the full date, which matters once paging reaches an earlier day's entries.

## Log Settings

The log levels the instance is running with, by tag. Rows marked *Configured* come from the instance's own configuration; rows marked *Override* were saved for this instance from this page and replace the configured level until they are removed.

Changing a configured row's level makes it an override, and the undo button reverts it. A change is pushed to the running service straight away and saved, so the service applies it again at every start.

An instance that is not connected cannot report what it runs with, so only its saved overrides are listed; a change made then is picked up when the service next starts.

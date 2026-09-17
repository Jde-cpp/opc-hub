# Applications

The [Applications](/apps) page shows every service that is registered with the application server: the program, the instance name, how long it has been up, its memory and the host it runs on. Open a card for the instance's own page.

## Application server

An application server instance has two tabs.

**Logs** shows the entries this instance has sent, newest first, a page at a time. The *Level* filter hides everything below the level picked; the refresh button reloads from the top; the view buttons switch between saved views, and the edit button beside them chooses which columns are shown and how they are sorted, and saves that as a view. Column headers sort as well. The *Time* column shows the time of day; hover it for the full date, which matters once paging reaches an earlier day's entries. There is no narrowing by application, date or text: the page is one instance's log.

**Log Settings** lists the log levels the instance is running with, by tag. Rows marked *Configured* come from the instance's own configuration; rows marked *Override* were saved for this instance from this page and replace the configured level until they are removed. Changing a configured row's level makes it an override, and the undo button reverts it. When the instance is not connected it cannot report what it runs with, so only its saved overrides are listed. A change is pushed to the running service straight away and saved, so the service applies it again at every start; a service that is not connected picks it up when it next starts.

## Gateways and OPC servers

A gateway or OPC server card opens that instance's page, described under [Gateways](/help/gateways).

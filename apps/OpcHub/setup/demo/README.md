# Jde OpcHub - the public demo's box

The runbook for the public demo ([`demo/spec.md`](../../../../docs/demo/spec.md), plan *Phases 8* to *10*): one VM,
`opchub.trypoply.com`, with the hub on loopback behind nginx, five emulated PLCs, the plant they make, and a visitor
who is a guest.

It takes two artefacts of one release, built by the Linux Release workflow:

- `jde-opchub_<version>_amd64.deb`, the package every install has ([`../linux/README.md`](../linux/README.md)).
- `jde-opc-demo-<version>.tar.gz`, the demo's own: the PLC emulator, the demo's settings, units, nginx sites and scripts.
  The package carries none of it, so no customer install has the demo.

## Building the tarball

```bash
apps/OpcHub/setup/linux/build-deb.sh     # the .deb, into <BuildDir>/setup
apps/OpcHub/setup/demo/build-demo.sh     # jde-opc-demo-<version>.tar.gz beside it, the .deb's version
```

The release tree needs `Jde.Opc.PlcEmulator` beside the package's targets. `build-demo.sh` takes the emulator with the
libraries it loads, as the package's tarball has its products, and the files listed in its header.

It also makes the smoke's Python modules, `proto/`, from the gateway socket's proto files. That takes apt's `protoc`,
3.21, `sudo apt install protobuf-compiler libprotobuf-dev`, or `--protoc`: the box and the watch read the modules with
apt's `python3-protobuf`, which takes no later `protoc`'s code, so the build tree's own is refused.

## Before the install

- The VM: Ubuntu 26.04, 2 vCPU, 4 GB. The name's A and AAAA records reach it: `dig +short opchub.trypoply.com`.
- The firewall first. It opens 80 and 443 and SSH by key from the admin's addresses, and nothing else.
    - Until the release and the review of the guest's surface close (plan 0.4), 443 is the admin's addresses alone.
    - The package's first start listens on every interface, 1967 and 4841, until `install-demo.sh` puts it on loopback.
- `sudo apt install nginx certbot python3-websockets python3-protobuf`. The last two are the smoke's.

## Install

```bash
sudo apt install ./jde-opchub_<version>_amd64.deb
tar xzf jde-opc-demo-<version>.tar.gz && cd jde-opc-demo-<version>
sudo ./install-demo.sh --email <the certbot account's address>
sudo /opt/jde-cpp/demo/smoke.sh --hub http://127.0.0.1:1967
```

`install-demo.sh` refuses a tarball of another version than the installed package. It runs in this order, so the 443
site never fronts a hub whose resources are not enforced, where every caller holds every right:

1. The files: the emulator and the scripts to `/opt/jde-cpp/demo`, the settings and units under `/etc`.
2. The port-80 site, which answers certbot's challenge and redirects the rest. `certbot certonly --webroot` issues the
   first certificate over it. A certificate already at certbot's path is used as found.
3. The hub restarted on the demo's config: loopback, the guest, nginx as the trusted front.
4. `seed.sh certs`: each PLC's two certificates, made as its unit would, and the seed's own pair.
5. The five PLC units enabled and started. Each logs in to the hub, which enrolls it as `plc<N>`.
6. `seed.sh plant`, below, which runs `seed.sh access` first.
7. The retention's timer, `jde-demo-retention.timer`.
8. The 443 site.

It ends by checking that nginx's own logrotate file keeps the access log 14 days, and says so where it does not.

`seed.sh access` logs in as `demo-seed`, a certificate user of its own whose key only root reads, and:

- grants itself `owner`, the shipped role that holds every right on every resource;
- purges the package's bundled `OpcServer` connection, which the demo never serves, and `flow-server`, which the package's first start made for the flow server the demo's config runs none of;
- creates the `guest` user and the `visitor` role with the pages' reads, and puts the guest in it;
- creates `demo-plc`, Read on `users`, and puts the PLCs in it;
- enforces every resource, last.

`seed.sh plant` runs `seed.sh access` first, waiting for the five PLCs' users, a minute by default, and dies where one
has not logged in. Then it makes the plant as the same user, every resource enforced, `owner` holding every right:

- five connections, `plc1` to `plc5`, on `opc.tcp://127.0.0.1:4842` to `:4846`, each with its PLC's applicationUri.
  `plc1` is the default;
- one template, `machine`, over the PLC's five variables, with no threshold, so every change is stored;
- a group from it for each connection, `filler`, `capper`, `labeler`, `case-packer` and `palletizer`;
- the reasons: Jam 1, Starved 2, Fault 3, Changeover 10, Cleaning 11 and No orders 20;
- the `plant` calendar in `America/New_York`: three eight-hour shifts from 06:00 every day, a half-hour break in each;
- the five machines, each on its group and the calendar, its rows starting at the seed's run;
- the site, `plant`, with its two lines, `bottling` and `packing`;
- the site's floor, from `floor.json` beside the script.

Every step is looked up by slug first, so a second run changes nothing and a partial one is finished by the next.

- **What the owner edits is kept.** An upgrade's seed doesn't undo an edit made on the page.
    - A machine is found by its group, so one renamed keeps its new slug.
    - The calendar's shifts are made only where it has none. One with any is the owner's.
- **The floor.** `floor.json` is the floor editor's export. To change the shipped floor, edit it on the page, export
  it, and replace the file in the repository.
    - A floor with no shape takes the file's.
    - A floor with any shape is kept as the editor left it, a shape deleted on the page included. A run that died
      part-way through the floor leaves it part-made: delete its shapes on the page and run `seed.sh plant` again.

## The first sign-in

The admin signs in with Google, through an SSH tunnel: the hub's client id is registered for `http://localhost:1967`
alone, and the public page offers no sign-in.

1. `ssh -L 1967:127.0.0.1:1967 <the box>`, then browse to `http://localhost:1967`. The page comes straight from the
   hub, which has no `config.json`, so it keeps its sign-in.
2. Sign in with Google. The first sign-in enrolls the user, with no role.
3. On the box: `sudo /opt/jde-cpp/demo/seed.sh grant <the Google address>`. The user is now an `owner`.

The owner's half of the smoke from the admin's machine, through the tunnel, takes a signed-in browser's session:
`JDE_SESSION=<session> ./smoke.sh --hub http://localhost:1967`.

## Upgrade

```bash
sudo systemctl stop nginx
sudo apt install ./jde-opchub_<version>_amd64.deb
tar xzf jde-opc-demo-<version>.tar.gz && cd jde-opc-demo-<version>
sudo ./install-demo.sh
sudo /opt/jde-cpp/demo/reset.sh --yes     # until the first release, where the release changed the tables or the history's files
sudo /opt/jde-cpp/demo/smoke.sh --hub http://127.0.0.1:1967
```

- nginx stops first. The package's upgrade restarts the hub, and a table a release adds ships unenforced until the
  seed runs. `install-demo.sh` starts nginx on port 80 alone, runs the seed, and enables the 443 site last.
- Until the first release, a release may change the database's tables or the historian's files with no conversion.
  Where it did, take the reset, below, after `install-demo.sh`.
    - Where `install-demo.sh` stopped because the hub does not start on the old database, reset, then run
      `install-demo.sh` again: it enables the 443 site.
- A file under `/etc` that was edited on the box is kept. The release's copy lands beside it as `.new`, and the script
  names each one: merge by hand.

## Reset

```bash
sudo /opt/jde-cpp/demo/reset.sh --yes
sudo /opt/jde-cpp/demo/smoke.sh --hub http://127.0.0.1:1967
```

The plant from nothing. Until the first release no database or historian file has to survive a change.

- It removes the hub's database, `/var/lib/Jde-Cpp/OpcHub/OpcHub.db`, and the historian's directory,
  `/var/lib/Jde-Cpp/OpcHub/hist/opc-gateway`. Every machine's figures and history start again.
- It keeps the certificates, the settings and the logs. Each PLC is enrolled again by its next login, and the admin by
  the next sign-in: run `seed.sh grant <the Google address>` after it.
- The order is the install's: the 443 site out, the units stopped, the two removed, the hub and the PLCs started, the
  seed whole, and the 443 site back last.
- Where the 443 site was on, a reset that fails part-way leaves `/var/lib/Jde-Cpp/OpcHub/reset-fronted`, so the run
  that finishes it puts the site back.
- Without `--yes` it says what it would remove, and removes nothing.

## The smoke

```bash
sudo /opt/jde-cpp/demo/smoke.sh --hub http://127.0.0.1:1967     # on the box, after every deploy
./smoke.sh                                                       # from anywhere else:  the public name
./smoke.sh --quick                                               # the watch's:  seconds
```

Each check is a line, `ok` or `FAILED`, and the exit is 0 when none failed. It needs `curl`, `python3`,
`python3-websockets` and `python3-protobuf`, and `smoke.py` and `proto/` beside it: copy the four out of the tarball's
`opt/jde-cpp/demo` to run it elsewhere.

- **The front.** The certificate, port 80's redirect, the page with its three headers, and `config.json`: the page's
  own port, no sign-in, Access and Applications closed, and `home` the plant's floor under a gateway the hub has.
- **The charts.** The site answers Highcharts' `highstock.js`, below. `--quick` does not ask for it.
- **A visitor.** A call with no session is given the guest's. The guest reads the connections, each with whether it
  is up, and the plant, and is refused users, sessions, logs, a connection's sessions and its last error, the
  processes' details, the flows and every write tried.
- **The plant.** Each line's figures by shift, each machine's engine live and its count's history, and the floor
  offering the guest no Edit.
- **The socket**, `smoke.py`: what the floor page does. A guest's socket on `/opc` takes its session and subscribes
  each machine, each line and the site, eight subscriptions on the seeded plant.
    - A machine's state is read from its ack. Unknown fails: no engine runs it.
    - A machine that runs in planned time and has not counted for 30 s and three of its cycles fails. Its PLC, or the
      collector's session on it, is down: until the collector is back the machine reads as it last was.
    - The Filler, which counts every second, is given 11 s to push its figures. In a stop or a break it is not
      waited on.
- **The owner**, with `--hub`: the hub answering the page with no `config.json`, `users` read, the five connections
  Connected, and the floor offering Edit.
- `--quick` is the certificate, the page and the socket alone.
- `--resolve <address>` dials the name at an address, before the DNS records exist. `--insecure` takes any
  certificate, and says the certificate was not checked.

The floor itself is checked in a browser: `https://opchub.trypoply.com` lands on the floor, every machine outlined and
its figures moving, and the browser's console shows no error.

## The charts

```bash
sudo /opt/jde-cpp/demo/highcharts.sh         # once the licence is held
/opt/jde-cpp/demo/highcharts.sh --check      # there, missing, or another release's
```

The OEE pages' charts and the History tab's trend draw with Highcharts Stock, an add-on. Its licence is a commercial
one, so neither the package nor the tarball carries it, and the site's owner holds the licence a public site needs.

- `highcharts.sh` fetches the `highcharts` npm package at the version this release's site is written against, checks
  it against that version's integrity, and lays its `esm` folder, the minified modules alone, at
  `/opt/jde-cpp/web/addons/highcharts/esm`. A page loaded after it draws its charts.
- Without the add-on each of those pages says the chart is not installed and shows its tables, the browser's console
  shows a 404 for it, and the smoke fails on it.
- `--from <highcharts-<version>.tgz>` takes a tarball already on the box, checked the same way.
- A package upgrade leaves the folder. `install-demo.sh` ends by saying whether the add-on is there and of this
  release's version: run `highcharts.sh` again where it is another's.
- `highcharts.sh --remove` takes it out.

## Operating

| what | where |
|---|---|
| the site's `config.json` | `/etc/jde-cpp/demo/config.json`: the port, the banner, `login` false, and `closed`, the sections the public page does not offer, Access and Applications. An edit shows at the next page load |
| the nginx sites | `/etc/jde-cpp/demo/nginx-demo-80.conf` and `nginx-demo.conf`, linked from `/etc/nginx/sites-enabled`. After an edit: `sudo nginx -t && sudo systemctl reload nginx` |
| a PLC's figures | `/etc/jde-cpp/apps/OpcServer/emulator/config/demo/plc<N>.jsonnet`, then `sudo systemctl restart jde-plc@<N>` |
| the hub's config | `/etc/jde-cpp/apps/OpcHub/config/Opc.Hub.Demo.jsonnet` and `args/demo/args.libsonnet`, run by the drop-in `/etc/systemd/system/jde-opchub.service.d/demo.conf` |
| the logs | `journalctl -u jde-opchub`, Warning and above; every level in `/var/lib/Jde-Cpp/OpcHub/Opc.Hub.log` and on the Logs page. `journalctl -u 'jde-plc@*'`. `/var/log/nginx`, kept 14 days by the nginx package's `/etc/logrotate.d/nginx` |
| the history's retention | `jde-demo-retention.timer`, daily: `/opt/jde-cpp/demo/retention.sh` removes the history's day directories older than 90 days. `journalctl -u jde-demo-retention` has its line; `sudo systemctl start jde-demo-retention` runs it now |
| the data | `/var/lib/Jde-Cpp`: `OpcHub/OpcHub.db` the database, `OpcHub/hist/opc-gateway/` the machines' history, a directory a day, `PlcEmulator.plc<N>/` and `PlcEmulator.plc<N>.opc/` each PLC's certificates, `DemoSeed/` the seed's |
| the plant | `/opt/jde-cpp/demo/seed.py`'s tables and `floor.json`. `sudo /opt/jde-cpp/demo/seed.sh plant` makes what is missing |
| the certificate | certbot's timer renews it over port 80, and `/etc/letsencrypt/renewal-hooks/deploy/jde-demo` reloads nginx. `sudo certbot renew --dry-run` checks it |

- **Enforced resources.** A resource is enforced while its row is live, and unenforced means every right for every
  caller, the guest included. Run `sudo /opt/jde-cpp/demo/seed.sh access` after anything that adds a resource.
- **A PLC out of `demo-plc`.** Each PLC looks a session's certificate up as its own hub user, a read of `users`. Out
  of the role it refuses the hub's collector, `BadIdentityTokenInvalid` on its connection, and its machine's history
  stops. `seed.sh access` puts it back, and the collector returns within two minutes.
- **Retention.** The historian never deletes. The timer's script removes a day's directory, every group's files of
  that day, once it is 90 days old, and a month or a year it empties. The OEE rows are the database's, and kept.
    - A day holding a temp file is left and named: a rewrite of that day is at work, or a crash cut one short, which
      the hub removes at its next start.
    - `retention.sh --dry-run` says what it would remove. `--days <n>` keeps another number of days.
- **nginx's access log** holds each visitor's address. The nginx package's logrotate file keeps it daily for 14 days:
  `install-demo.sh` checks that file and `logrotate.timer`, and replaces neither.
- **The hub's files.** The drop-in gives the hub 65,536 open files. A visitor is two sockets, and one on a PLC's node
  browser a session to the PLC besides.
- **A PLC's sessions.** A PLC takes 200 sessions, the collector's among them, and a visitor's ends two minutes after
  its last use. Past that a visitor on its node browser is refused, `BadTooManySessions`, and the collector keeps
  its own.
- **The policy.** `nginx-demo.conf` sends it as `Content-Security-Policy-Report-Only`. The comment above that line
  says how to enforce it, at the opening.
- **Back to the package's hub:** `sudo systemctl revert jde-opchub && sudo systemctl restart jde-opchub`, and
  `sudo systemctl disable --now jde-plc@{1,2,3,4,5} jde-demo-retention.timer`. Remove the two links under
  `/etc/nginx/sites-enabled` first: the package's unit listens on every interface.

## Watching

Two checks from outside the box, every five minutes.

- **A hosted monitor**, the owner's account with any provider. It fetches `https://opchub.trypoply.com/` every five
  minutes and expects a 200, and it reads the certificate's expiry, alerting two weeks ahead: certbot renews at 30
  days left, so less than that is a renewal that failed.
- **The Demo Watch workflow**, [`demo-watch.yml`](../../../../.github/workflows/demo-watch.yml), for the socket. On a
  GitHub-hosted runner it takes `smoke.sh`, `smoke.py` and `proto/` from the latest release's demo tarball and runs
  `smoke.sh --quick` against the public name.
    - It runs while the repository variable `DEMO_WATCH` is `on`: Settings, Secrets and variables, Actions,
      Variables. Set it once the box runs a release that carries the tarball. Clear it while the box is down on
      purpose.
    - `DEMO_URL`, where set, is the site in place of `https://opchub.trypoply.com`.
    - A failed run is mailed by GitHub to whoever last changed the workflow's schedule, under their notification
      settings: Notifications, Actions, failed workflows.
    - GitHub's schedule is five minutes at best and late under load, which is why the monitor keeps the five
      minutes. It also stops a schedule after 60 days with no commit to the repository.

On the box, `journalctl -u jde-opchub --since yesterday` is what went wrong, the hub writing Warning and above there. A start writes some sixty
lines there: the security policies the hub's certificate cannot carry, the PLC certificates that name no address, and
each PLC refusing the collector until its own login is back.

## The soak

A week on the box before the name is published, the 443 port held to the admin's addresses. The charts' add-on is
laid first, so the week's policy reports are the site's as it will open.

- **Twice a day**, from outside the box: `./smoke.sh`.
- **Daily**, on the box: the hub's and the PLCs' journals since the day before, and
  `journalctl -u jde-demo-retention`.
- **The figures.** Each machine is to read an OEE between about 60 and 85 percent over a shift, and the two lines
  apart. Read them on the OEE dashboard by shift, as the owner through the tunnel.
    - A machine's figures are its overlay's, `plc<N>.jsonnet`: `stops`, each a mean gap with its shortest and
      longest, or a time of day with its length; `slow`, a time of day, a length and a factor on the cycle; `reject`.
      A stop under two minutes is a small stop, a performance loss. A longer one is an availability loss.
    - After an edit: `sudo systemctl restart jde-plc@<N>`. The minutes after a restart read a performance over
      100 percent, the gap's counts landing when the collector is back, so judge a change by the next whole shift.
    - A figure that settles goes into the repository's overlay and the spec's table, so the next release ships it.
- **Visitors on a PLC's node browser**, on the box:

  ```bash
  /opt/jde-cpp/demo/smoke.py guests 150                       # 150 visitors on plc1, held a minute
  /opt/jde-cpp/demo/smoke.py guests 300 --connection plc3     # past a PLC's 200 sessions
  ```

  Each is a guest's session at an address of its own, a socket and a subscription to the machine's five variables,
  straight at the hub: nginx would hold one address to 20 sockets. It says how many were taken and what refused the
  rest.
    - Watch the collector meanwhile, as the owner: the Connections tab, or
      `serverConnections{ slug connectionStatus{ name error } }`. The machine's count keeps arriving every second.
    - 150 are all taken. Of 300, 199 are, and the rest are refused `BadTooManySessions`.
- **The policy's reports.** The policy is report-only through the soak, so a violation is a line in the browser's
  console, prefixed `[Report Only]`. As a guest, with the console open, walk the floor, a machine's panel and its
  links, the OEE dashboard, a line's and a machine's OEE pages, a node's History tab, and the help.
    - Two refusals are a visitor's own and not the policy's: a group's page is refused the gateway's endpoint, and the
      Connections list its session counts, which it then asks without. Each is one 403 in the console.

## The opening

In this order, once the soak's week is clean.

1. **No advisory open.** Every security advisory filed since the release's pre-auth review is closed or published:
   the repository's Security tab, Advisories, shows no draft and no triage.
2. **The charts.** The licence a public site needs is held, and `highcharts.sh --check` says the add-on is on the
   site at this release's version.
3. **`home`.** `/etc/jde-cpp/demo/config.json` names it, the plant's floor, as the release's file does. An edited file
   on the box takes it from `config.json.new`. The smoke checks it.
4. **The policy enforced.** In `/etc/jde-cpp/demo/nginx-demo.conf`, rename the header
   `Content-Security-Policy-Report-Only` to `Content-Security-Policy`, then
   `sudo nginx -t && sudo systemctl reload nginx`. Walk the soak's pages again as a guest: no console error.
    - The edit is the box's, so an upgrade keeps it and lays the release's file beside it as `.new`. Make the same
      rename in the repository's [`nginx-demo.conf`](../linux/nginx-demo.conf), so the next release ships it
      enforced.
    - The admin's page through the tunnel comes straight from the hub and never meets the policy.
5. **The smoke**, from outside the box, green.
6. **The watch on:** the hosted monitor made, and `DEMO_WATCH` set to `on`.
7. **The firewall** opens 443 to everyone, and the name is published.
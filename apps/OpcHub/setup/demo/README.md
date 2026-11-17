# Jde OpcHub - the public demo's box

The runbook for the public demo ([`demo/spec.md`](../../../../docs/demo/spec.md), plan *Phases 8* and *9*): one VM,
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

## Before the install

- The VM: Ubuntu 26.04, 2 vCPU, 4 GB. The name's A and AAAA records reach it: `dig +short opchub.trypoply.com`.
- The firewall first. It opens 80 and 443 and SSH by key from the admin's addresses, and nothing else.
    - Until the release and the review of the guest's surface close (plan 0.4), 443 is the admin's addresses alone.
    - The package's first start listens on every interface, 1967 and 4841, until `install-demo.sh` puts it on loopback.
- `sudo apt install nginx certbot`.

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
6. `seed.sh access`, below.
7. `seed.sh plant`, below.
8. The 443 site.

`seed.sh access` logs in as `demo-seed`, a certificate user of its own whose key only root reads, and:

- grants itself `owner`, the shipped role that holds every right on every resource;
- purges the package's bundled `OpcServer` connection, which the demo never serves, and `flow-server`, which the package's first start made for the flow server the demo's config runs none of;
- creates the `guest` user and the `visitor` role with the pages' reads, and puts the guest in it;
- creates `demo-plc`, Read on `users`, and puts the PLCs in it;
- enforces every resource, last.

`seed.sh plant` waits for the five PLCs' users, a minute by default, and makes the plant as the same user:

- five connections, `plc1` to `plc5`, on `opc.tcp://127.0.0.1:4842` to `:4846`, each with its PLC's applicationUri.
  `plc1` is the default;
- one template, `machine`, over the PLC's five variables, with no threshold, so every change is stored;
- a group from it for each connection, `filler`, `capper`, `labeler`, `case-packer` and `palletizer`;
- the reasons: Jam 1, Starved 2, Fault 3, Changeover 10, Cleaning 11 and No orders 20;
- the `plant` calendar in `America/New_York`: three eight-hour shifts from 06:00 every day, a half-hour break in each;
- the five machines, each on its group and the calendar, its rows starting at the seed's run;
- the site, `plant`, with its two lines, `bottling` and `packing`;
- the site's floor, from `floor.json` beside the script;
- `seed.sh access` again, last.

Every step is looked up by slug first, so a second run changes nothing and a partial one is finished by the next.

- **The floor.** `floor.json` is the floor editor's export. To change the shipped floor, edit it on the page, export
  it, and replace the file in the repository.
    - A floor on the box that no longer matches the file is kept as the editor left it.
    - A floor that is part of the file's takes the shapes it lacks.

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
- Without `--yes` it says what it would remove, and removes nothing.

## Operating

| what | where |
|---|---|
| the site's `config.json` | `/etc/jde-cpp/demo/config.json`: the port, the banner, `login` false, and `closed`, the sections the public page does not offer, Access and Applications. An edit shows at the next page load |
| the nginx sites | `/etc/jde-cpp/demo/nginx-demo-80.conf` and `nginx-demo.conf`, linked from `/etc/nginx/sites-enabled`. After an edit: `sudo nginx -t && sudo systemctl reload nginx` |
| a PLC's figures | `/etc/jde-cpp/apps/OpcServer/emulator/config/demo/plc<N>.jsonnet`, then `sudo systemctl restart jde-plc@<N>` |
| the hub's config | `/etc/jde-cpp/apps/OpcHub/config/Opc.Hub.Demo.jsonnet` and `args/demo/args.libsonnet`, run by the drop-in `/etc/systemd/system/jde-opchub.service.d/demo.conf` |
| the logs | `journalctl -u jde-opchub`, `journalctl -u 'jde-plc@*'`, `/var/log/nginx` |
| the data | `/var/lib/Jde-Cpp`: `OpcHub/OpcHub.db` the database, `OpcHub/hist/opc-gateway/` the machines' history, a directory a day, `PlcEmulator.plc<N>/` and `PlcEmulator.plc<N>.opc/` each PLC's certificates, `DemoSeed/` the seed's |
| the plant | `/opt/jde-cpp/demo/seed.py`'s tables and `floor.json`. `sudo /opt/jde-cpp/demo/seed.sh plant` makes what is missing |
| the certificate | certbot's timer renews it over port 80, and `/etc/letsencrypt/renewal-hooks/deploy/jde-demo` reloads nginx. `sudo certbot renew --dry-run` checks it |

- **Enforced resources.** A resource is enforced while its row is live, and unenforced means every right for every
  caller, the guest included. Run `sudo /opt/jde-cpp/demo/seed.sh access` after anything that adds a resource.
- **A PLC out of `demo-plc`.** Each PLC looks a session's certificate up as its own hub user, a read of `users`. Out
  of the role it refuses the hub's collector, `BadIdentityTokenInvalid` on its connection, and its machine's history
  stops. `seed.sh access` puts it back, and the collector returns within two minutes.
- **The smoke.** Its last lines read the plant as a visitor: the two lines' figures by shift, each machine's engine
  and history, and `codeOee` and `createHistory` refused. With `--hub`, the owner's half checks the five connections.
- **The policy.** `nginx-demo.conf` sends it as `Content-Security-Policy-Report-Only`. The comment above that line
  says how to enforce it, at the opening.
- **Back to the package's hub:** `sudo systemctl revert jde-opchub && sudo systemctl restart jde-opchub`, and
  `sudo systemctl disable --now jde-plc@{1,2,3,4,5}`. Remove the two links under `/etc/nginx/sites-enabled` first: the
  package's unit listens on every interface.
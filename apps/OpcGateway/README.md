# OpcGateway

Rest/Websocket Application on top of [open62541.org](https://www.open62541.org/).

## Installation
### Prerequisites
1) The [Microsoft Visual C++ 2015-2022 x64 redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe). An all-users install adds it when missing; a current-user install has no administrator rights and expects it present.

### Steps
1) Run `OpcHubSetup-<version>.exe` - built from [`apps/OpcHub/setup`](../OpcHub/setup/README.md), which also documents the installed layout.
   - Install mode: **All users** registers the selected products as Windows services (administrator rights); **Current user** installs under `%LOCALAPPDATA%\Programs` and runs them from Start Menu shortcuts (no administrator rights).
   - Components: the OPC Hub (`Jde.OpcHub` - the AppServer and the OpcGateway in one process, required), the OPC UA Server (`Jde.OpcServer`, optional), the Web UI (served by the hub).

   The database is sqlite, one file per product under `C:\ProgramData\Jde-Cpp\<Product>`, created on first start - no SQL Server, no setup script.
2) Browse to http://localhost:1967/ once `Jde.OpcHub` runs - the hub serves the Web UI itself; from another machine, `http://<hub>:1967/`. IIS is optional: to put the site behind it, physical path `C:\Program Files\Jde-Cpp\Web` (or the current-user install's `Web` dir), port 8071

  a.  Features:  ![](./doc/iis-features.png)

  b.  Settings:  ![](./doc/iis-site-settings.png)

  c.  Deep links: reloading or bookmarking a route (`/login`, `/apps/gateways`) is a 404 under IIS until the [URL Rewrite module](https://www.iis.net/downloads/microsoft/url-rewrite) is installed and the `<rewrite>` rule in `Web\web.config` is uncommented (it sends every path that is not a file to `index.html`). The hub's own url needs no such step.

3) Log in with Google - with the OPC UA Server component installed (it seeds the login provider and the hub's default connection); the site's origin must be registered under the OAuth client id the hub serves - [`apps/OpcHub/setup/README.md`](../OpcHub/setup/README.md) "First login".
4) To uninstall: Settings > Apps (a current-user install also has an Uninstall shortcut in its Start Menu folder). The database, certificates and logs under `C:\ProgramData\Jde-Cpp` are left in place.

### Linux
1) `sudo apt install ./jde-opchub_<version>_amd64.deb` - built from [`apps/OpcHub/setup/linux`](../OpcHub/setup/linux/README.md), which also documents the installed layout. Ubuntu 24.04 or later.
   - The hub runs as the `jde-opchub` systemd service (port 1967, a `jde-cpp` account); the OPC UA server is installed but not enabled: `sudo systemctl enable --now jde-opcserver`.
   - The database is sqlite, one file per product under `/var/lib/Jde-Cpp/<Product>`, created on first start.
   - Without root: the tarball's `install.sh` installs under your account and runs the products as `systemctl --user` units.
2) nginx - the site file is installed, not enabled:
```
    sudo ln -s /etc/jde-cpp/nginx-opchub.conf /etc/nginx/sites-enabled/jde-opchub && sudo systemctl reload nginx
```
3) Log in with Google: `sudo systemctl enable --now jde-opcserver` (it seeds the login provider and the hub's default connection); the site's origin must be registered under the OAuth client id the hub serves - [`apps/OpcHub/setup/README.md`](../OpcHub/setup/README.md) "First login".
4) To uninstall: `sudo apt remove jde-opchub` (`./install.sh --uninstall` for a per-user install). The database, certificates and logs under `/var/lib/Jde-Cpp` (`~/.config/Jde-Cpp`) are left in place.

## Running
1) Start the service(s)
```
    net start Jde.OpcHub
    net start Jde.OpcServer
```
   or, in a current-user install, the Start Menu shortcuts (each runs in its own console window). The standalone `Jde.AppServer` + `Jde.OpcGateway` pair (`apps/AppServer`, `apps/OpcGateway`) still builds for split deployments but is not installed.
2) Browse to http://127.0.0.1:1967/ - or from any machine that reaches the hub's port 1967, by the hub's name: `http://<hub>:1967/`. The page calls the hub by the name it was browsed by.
3) Setup Opc Server.
![](./doc/OpcServer.png)
   1) Click Settings.
   2) Opc Servers.
   3) Enter Opc Server information.
   4) Save.
4) Log in
![](./doc/login.png)
   1) [Id specified setting up the opc server]\\[Opc User Name]
   2) Password to Opc Server.

5) Browsing/Reading/Streaming/Writing.
![](./doc/Node.png)
   1) Click Opc Servers and browse to a node.
      1) Initially you may need to trust the client certificate on OpcServer.
   3) Click Snapshot to refresh values.
   4) Click checkbox next to a node to stream values.
   5) Enter new node value to write.

//The no-root install's args, on both platforms: the Windows current-user mode (apps/OpcHub/setup - its shortcuts, Run key
//and "Start now" pass -include=args/install-user) and the Linux tarball's `systemctl --user` units (setup/linux/install.sh).
//args/install - edit THAT file for the OAuth client id, host names, a certificate - plus a listen address.  Bound to
//loopback, the products answer this machine only, which is what an install without administrator rights can promise: it
//cannot open a firewall port, and on Windows a program listening on every interface gets Windows' own "allow this app?"
//prompt, which wants an administrator's credentials a standard user has no business being asked for
//(reviews/install-issues.md #16, #32).  To reach the products from another machine, install for all users - the .deb on
//Linux - or set listenAddress: null here and have an administrator open TCP 1967 (and 4840 for the OPC UA server) by hand.  A Windows reinstall overwrites this file - keep a copy; the tarball's install.sh keeps your edit, this release's beside it as .new.
(import '../install/args.libsonnet') + {
	listenAddress: "127.0.0.1",
}

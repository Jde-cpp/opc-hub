//The current-user install's args (apps/OpcHub/setup - the mode without administrator rights; its shortcuts, Run key and
//"Start now" pass -include=args/install-user): args/install - edit THAT file for the OAuth client id, host names, a
//certificate - plus a listen address.  Bound to loopback, the products answer this machine only, which is what a profile
//install can promise: it cannot open Windows Firewall, and a program listening on every interface gets Windows' own
//"allow this app?" prompt, which wants an administrator's credentials a standard user has no business being asked for
//(reviews/install-issues.md #16).  To reach the products from another machine, install for all users - or set
//listenAddress: null here and have an administrator open TCP 1967 (and 4840 for the OPC UA server) by hand.
(import '../install/args.libsonnet') + {
	listenAddress: "127.0.0.1",
}

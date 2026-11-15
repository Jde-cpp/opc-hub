//The install args on a dev box:  the nodesets read from the source tree, since no dev box has the tarball's nodesets dir.
//`-settings=config/demo/plc<N>.jsonnet -include=../args/dev` runs a demo PLC against a dev hub (README *Demo*); everything else
//- the logs and certificates under $(ProgramData)/Jde-Cpp, the hub's login anchor - is the install's.
(import '../install/args.libsonnet') + {
	nodesetsDir: "$(JDE_DIR)/apps/OpcServer/config/nodesets"
}
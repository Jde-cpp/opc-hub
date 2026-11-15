local paths = import '../../../../../../libs/db/config/paths-common.libsonnet';
//The installed layout (apps/OpcHub/setup/demo, the demo tarball - the .deb ships no emulator):  the logs and the nodesets under
//the company root's PlcEmulator dir, the certificates under each PLC's own product dirs (config/demo).  paths-common only -
//the service starts without -tests, so no ext vars (which is why sqlite-common is not imported: it binds buildTarget/logsDir/
//windows/path).  Loaded through Opc.PlcEmulator.Install.jsonnet, which replaces the base config's dev-shaped fields (the
//ext vars, the source-tree pubsub import, the $(JDE_DIR) nodeset) from here.
paths + {
	local args = self,
	local emulatorDir = args.companyDir+"/PlcEmulator", //companyDir is the one spelling of the company root - see paths-common.
	logsDir: emulatorDir, //a demo PLC's overlay puts its own log beside its login certificates instead (config/demo/demo.libsonnet).
	//The text log (<logsDir>/Opc.PlcEmulator.log): a start rolls the previous run aside as .1.log … .3.log instead of erasing
	//it, and the file rolls at 10 MB too - so the log of a failure survives the restart that recovers from it
	//(reviews/install-issues.md #13).  The dev configs keep one file.
	logFile: { keep: 3 },
	//The console sink - the journal - without the source locations the dev defaults give it (libs/fwk/src/log/SpdLog.cpp):  a
	//link to, or the path of, a file on the build machine, which no install has (reviews/install-issues.md #49).
	logConsole: { pattern: "%^%3!l%$-%H:%M:%S.%e %v" },
	nodesetsDir: emulatorDir+"/nodesets" //where the tarball puts demo.NodeSet2.xml, the one nodeset it lays (demo plan Phase 8), for config/demo;  the Install config's own pumps.NodeSet2.xml is a dev box's (args/dev).
}
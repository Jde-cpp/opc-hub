#pragma once

namespace Jde::Opc::Server{
	struct UAServer;
	//The address space from the NodeSet2 files at /opcServer/configFiles, loaded before Run.
	α LoadNodeset( UAServer& ua, fs::path configFile, SRCE )ε->void;
	//After the nodesets:  the callbacks of the methods pumps.NodeSet2.xml declares, which a nodeset cannot carry - a method
	//without one answers BadInternalError.  Nothing without the pumps namespace.
	α BindPumpMethods( UAServer& ua )ι->void;
	//After the last nodeset and before Run:  the types the nodesets added, published to PubSub (see the definition).
	α PublishDataTypes( UAServer& ua )ι->void;
}
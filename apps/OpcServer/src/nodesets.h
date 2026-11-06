#pragma once

namespace Jde::Opc::Server{
	struct UAServer;
	//The address space from the NodeSet2 files at /opcServer/configFiles, loaded before Run.
	α LoadNodeset( UAServer& ua, fs::path configFile, SRCE )ε->void;
	//After the nodesets:  what the pumps demo (pumps.NodeSet2.xml) needs that a nodeset cannot carry - its method's callback and a
	//WriteMask on one pump (see the definition).  Nothing without the pumps namespace.
	α ConfigurePumps( UAServer& ua )ι->void;
	//After the last nodeset and before Run:  the types the nodesets added, published to PubSub (see the definition).
	α PublishDataTypes( UAServer& ua )ι->void;
}
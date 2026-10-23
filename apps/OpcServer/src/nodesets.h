#pragma once

namespace Jde::Opc::Server{
	struct UAServer;
	//The address space from the NodeSet2 files at /opcServer/configFiles, loaded before Run.
	α Load( UAServer& ua, fs::path configFile, SRCE )ε->void;
	//After the last nodeset and before Run:  the types the nodesets added, published to PubSub (see the definition).
	α PublishDataTypes( UAServer& ua )ι->void;
}
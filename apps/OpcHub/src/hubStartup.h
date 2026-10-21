#pragma once
#include <jde/fwk.h>

namespace Jde::Opc::Hub{
	//Blocks until both roles are up behind ONE listener: the AppServer (app/access schemas, its listener) and the gateway
	//(gateway schema) configured, one HubQL over all three schemas, one Hub::RequestHandler on httpSettings' port, then the
	//gateway wired to the AppServer in-process.  Call from a thread that does not run the io pool (main, a test main).
	//afterWebServer: runs once the listener is up, before the gateway's access step - where an embedding test starts a co-hosted
	//OpcServer (it logs in to the AppServer role over the socket).
	α Startup( jobject httpSettings, function<void()> afterWebServer={} )ε->void;
}

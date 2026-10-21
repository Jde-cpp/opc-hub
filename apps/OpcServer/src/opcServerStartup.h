#pragma once

namespace Jde::Opc::Server{
	//Blocks until the server is up: schema sync, web server, AppServer connect, access configure, UA server run, then the
	//db/mutation/config-file loads.  Call from a thread that does not run the io pool (main) - each step blocks on an awaitable.
	α Startup( jobject webServerSettings )ε->void;
}

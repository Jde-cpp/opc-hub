#pragma once
#include <jde/fwk/usings.h>

namespace Jde::DB{ struct AppSchema; }
namespace Jde::QL{ struct LocalQL; }
namespace Jde::Opc::Gateway{
	//Startup in three parts, so a host that embeds the gateway (OpcHub) can put its own listener and QL between them:
	//Configure - the schema (synced unless `ql` is a host's, which then owns the sync), the QL (the gateway's, or `ql`), the
	//introspection, the ping/ttl settings, the certificate the app client authenticates with; returns the schema.  Last, it opens
	//server_connections' custom mutation (ServerCnnctnMAwait::SetSynced), which the seed inside the sync skips.
	α Configure( const jobject& webServerSettings, sp<QL::LocalQL> ql={} )ε->sp<DB::AppSchema>;
	//Connect - the AppServer link: the login + socket unless the app client is local, this instance's log settings, the access
	//snapshot and the shutdown hooks.  Blocks on awaitables: call from a thread that does not run the io pool (main).
	α Connect( sp<DB::AppSchema> schema )ε->void;
	//Blocks until the gateway is up: Configure, the web server, then Connect.
	α Startup( jobject webServerSettings )ε->void;
}
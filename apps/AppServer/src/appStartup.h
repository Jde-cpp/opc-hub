#pragma once
#include "usings.h"
#include <jde/db/awaits/DBAwait.h>
#include <jde/db/awaits/ExecuteAwait.h>

namespace Jde::DB{ struct AppSchema; }

namespace Jde::QL{ struct LocalQL; }
namespace Jde::Access{ struct Authorize; }
namespace Jde::App::Server{
	α InitLogging()ι->void;
	//A host embedding the AppServer beside another role (OpcHub) adds that role's schemas to the QL, the sync and the access
	//snapshot, and supplies the QL over all of them.  Defaults: the AppServer's own two schemas and its AppServerQL.
	struct ConfigureOptions{
		vector<sp<DB::AppSchema>> ExtraSchemas;
		function<sp<QL::LocalQL>(vector<sp<DB::AppSchema>>, sp<Access::Authorize>)> MakeQL;
	};
	//Everything but the listener: db, schema sync, the access snapshot and its listener, this process's connection row, the
	//signing key and the QL hooks.  webServerSettings for its `ssl` (the key the JWTs are signed with).
	α Configure( const jobject& webServerSettings, ConfigureOptions options={} )ε->void;
	α AppStartup( jobject webServerSettings )ε->void;//Configure + the listener + this instance's log settings.
	α AppSchema()ι->sp<DB::AppSchema>;
}
namespace Jde::App{
	//The pks AddConnection mints for one registration - one value, so a reader takes all three from the same publish.
	struct ConnectionPks{ ProgramPK Program{}; ProgInstPK Instance{}; ConnectionPK Connection{}; };
	//Inserts this process's/an instance's connection row and re-loads the `hosts` enum the row may have grown.  The re-load
	//is synchronous (QL::LoadEnum) - fine from startup and the tests, which are off the executor pool.  reloadHosts=false
	//for the request path, which awaits ReloadHosts instead:  a BlockAwait on an executor thread over a query that needs
	//that pool is the wedge of emulator-review W1 / db-review3 #1.
	//resourceSchema:  the instance's own access schema, kept on the instance row as given - see RegistrationSchema, InstanceLogSchema.
	α AddConnection( str applicationName, str instanceName, str hostName, uint pid, str resourceSchema={}, bool reloadHosts=true )ε->ConnectionPks;
	//The schema a socket registration leaves on its (program, name) instance:  `claimed` (its auth_resource) when `registrant`
	//administers both it and the schema the instance already has - TestSchemaAdmin on each, so one with no active root passes -
	//else the one it has.  Otherwise any registrant decides whose `logs` governs another process's lines and tag levels.
	α RegistrationSchema( str applicationName, str instanceName, str claimed, UserPK registrant )ε->string;
	//The `hosts` re-load as an awaitable:  clears the cached map and returns the select that refills it.  co_await it through
	//Any() from any coroutine on the pool - the thread goes back to the pool while the query runs.
	α ReloadHosts( SRCE )ε->DB::CacheAwait<flat_map<uint,string>>;
	//ends a *connection*, not a program instance - it stamps connections.deleted for one connection_id.
	α EndConnection( ConnectionPK connectionId, SRCE )ι->DB::ExecuteAwait::Task;

	//The schema of the `logs` resource an instance's forwarded lines and stored tag levels answer to:  the access schema it
	//registered with (`opc.install`), else the AppServer's own `app`.
	α InstanceLogSchema( ProgInstPK instancePK )ι->string;
	//The instances whose forwarded lines `user` may not read - the AppServer's logs query leaves them out.
	α HiddenLogInstances( UserPK user )ι->flat_set<ProgInstPK>;
}

#include <jde/access/awaits/ConfigureAwait.h>

#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/co/AllAwait.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/access/Authorize.h>
#include <jde/access/awaits/EventsSubscribeAwait.h>
#include "IdentityLoadAwait.h"
#include "AclLoadAwait.h"
#include "ResourceLoadAwait.h"
#include "RoleLoadAwait.h"
#include "../accessInternal.h"

#define let const auto

namespace Jde::Access{
	α ConfigureAwait::Run()ι->VoidTask{
		try{
			if( !Reload ){
				ResourceSyncAwait sync{ QlServer, Schemas, OpcServerInstance, Executer };
				co_await Any( sync );
				if( SyncOnly ){
					Resume();
					co_return;
				}
			}
			ResourceLoadAwait resourceLoad{ QlServer, Schemas, OpcServerInstance, Executer, AllSchemas };//noncopyable, so All references it;  the other three it owns.
			//The four at once (#345):  each only reads, none needs another's rows, and the snapshot still swaps once below - on a client they are
			//round trips to the AppServer, so a startup or reconnect waits for the slowest instead of the sum.
			auto [identities, resources, roles, acl] = co_await All( IdentityLoadAwait{QlServer, Executer}, resourceLoad, RoleLoadAwait{QlServer, Executer}, AclLoadAwait{QlServer, Executer} );
			Authorizer->Load( move(identities), move(resources), move(roles), move(acl) );
			if( !Reload ){//a reload's subscriptions are still live (Replay re-issued them) - it was only ever about the snapshot.
				vector<string> schemaNames;//empty = every schema:  the await then sends no predicate at all - an empty array would be an In filter matching nothing.
				if( !AllSchemas ){
					for( let& schema : Schemas )
						schemaNames.push_back( InstanceSchemaName(schema->Name, OpcServerInstance) );
				}
				EventsSubscribeAwait subscribe{ QlServer, move(schemaNames), Executer, Listener };
				co_await Any( subscribe );
			}
			Resume();
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
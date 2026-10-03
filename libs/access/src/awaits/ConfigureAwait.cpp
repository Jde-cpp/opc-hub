#include <jde/access/awaits/ConfigureAwait.h>

#include <jde/fwk/co/AnyAwait.h>
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
			auto identities = co_await Any( IdentityLoadAwait{QlServer, Executer} );
			ResourceLoadAwait resourceLoad{ QlServer, Schemas, OpcServerInstance, Executer, AllSchemas };
			auto resources = co_await Any( resourceLoad );
			auto roles = co_await Any( RoleLoadAwait{QlServer, Executer} );
			auto acl = co_await Any( AclLoadAwait{QlServer, Executer} );
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

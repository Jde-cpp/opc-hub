#include <jde/access/awaits/ConfigureAwait.h>

#include <jde/db/meta/AppSchema.h>
#include <jde/access/Authorize.h>
#include <jde/access/awaits/EventsSubscribeAwait.h>
#include "IdentityLoadAwait.h"
#include "AclLoadAwait.h"
#include "ResourceLoadAwait.h"
#include "RoleLoadAwait.h"


#define let const auto

namespace Jde::Access{
	struct Loader final{
		Ω Resources( ConfigureAwait& await )ι->TAwait<ResourcePermissions>::Task;
	private:
		Ω Acl( ConfigureAwait& await )->AclLoadAwait::Task;
		Ω Roles( ConfigureAwait& await )ι->RoleLoadAwait::Task;
		Ω Subscribe( ConfigureAwait& await )ι->EventsSubscribeAwait::Task;
	};

	α Loader::Subscribe( ConfigureAwait& await )ι->EventsSubscribeAwait::Task{
		try{
			vector<string> schemaNames;//empty = every schema:  the await then sends no predicate at all - an empty array would be an In filter matching nothing.
			if( !await.AllSchemas ){
				for( let& schema : await.Schemas )
					schemaNames.push_back( await.OpcServerInstance.size() ? Ƒ("{}.{}", schema->Name, await.OpcServerInstance ) : schema->Name );
			}
			co_await EventsSubscribeAwait{ await.QlServer, schemaNames, await.Executer, await.Listener };
			await.Resume();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}


	α Loader::Acl( ConfigureAwait& await )->AclLoadAwait::Task{
		try{
			let acl = co_await AclLoadAwait{ await.QlServer, await.Executer };
			{//released before Subscribe, as LoadUsers does:  Subscribe touches none of this, and holding the authorizer's mutex across it deadlocks anything downstream that authorizes.
				ul _{ await.Authorizer->Mutex };
				await.Authorizer->Acl = move( acl );
				await.Authorizer->SetUserPermissions( {} );
			}
			if( await.Reload )
				await.Resume();//the subscriptions are still live (Replay re-issued them) - this was only ever about the snapshot.
			else
				Subscribe( await );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}

  α Loader::Roles( ConfigureAwait& await )ι->RoleLoadAwait::Task{
		try{
			auto roles = co_await RoleLoadAwait{ await.QlServer, await.Executer };
			{
				ul _{ await.Authorizer->Mutex };
				await.Authorizer->Roles = move( roles );
			}
			Acl( await );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}

	α Loader::Resources( ConfigureAwait& await )ι->TAwait<ResourcePermissions>::Task{
		try{
			auto loaded = co_await ResourceLoadAwait{ await.QlServer, await.Schemas, await.OpcServerInstance, await.Executer, await.AllSchemas };
			{
				ul _{ await.Authorizer->Mutex };
				await.Authorizer->SchemaResources.clear();
				await.Authorizer->Resources.clear();
				for( let& [pk, resource] : loaded.Resources ){
					if( !resource.IsDeleted ){
						auto& slugResources = await.Authorizer->SchemaResources.try_emplace( resource.Schema ).first->second;
						auto& criteras = slugResources.try_emplace( resource.Slug ).first->second;
						criteras.try_emplace( resource.Criteria, pk );
					}
					await.Authorizer->Resources.emplace( pk, move(resource) );
				}
				await.Authorizer->Permissions = move( loaded.Permissions );
			}
			Roles( await );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}

	α ConfigureAwait::SyncResources()ι->VoidTask{
		try{
			co_await ResourceSyncAwait{ QlServer, Schemas, OpcServerInstance, Executer };
			if( SyncOnly )
				Resume();
			else
				LoadUsers();
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	};

	α ConfigureAwait::LoadUsers()ι->TAwait<Identities>::Task{
		try{
			auto identities = co_await IdentityLoadAwait{ QlServer, Executer };
			{
				ul _{ Authorizer->Mutex };
				Authorizer->Users = std::move( identities.Users );
				Authorizer->Groups = std::move( identities.Groups );
			}
			Loader::Resources( *this );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
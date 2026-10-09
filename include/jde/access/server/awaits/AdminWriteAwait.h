#pragma once
#include <jde/access/access.h>
#include <jde/ql/types/MutationQL.h>

//Writes a table's ops do not declare are the system's alone.  These have callers that are not, so Server::CustomMutation
//routes them here:  an admin check, then the stock mutation run as the system.
namespace Jde::Access::Server{
	struct AdminWriteAwait : TAwait<jvalue>, noncopyable{
		AdminWriteAwait( QL::MutationQL m, UserPK executer, SRCE )ι:TAwait<jvalue>{ sl }, _mutation{ move(m) }, _executer{ executer }{}
		α Suspend()ι->void override{ Execute(); }
	protected:
		β Test()ε->void=0;
		β Execute()ι->TAwait<jvalue>::Task;//Test, then the stock mutation as the system.
		QL::MutationQL _mutation;
		Jde::UserPK _executer;
	};

	//updatePermissionRight( id:42, allowed:255, denied:0 ) - the Permissions tab's save.  A known user with Administer on the grant's
	//resource, the right RoleMAwait::RemovePermission asks to drop it.
	struct PermissionRightMAwait final : AdminWriteAwait{
		using AdminWriteAwait::AdminWriteAwait;
	private:
		α Test()ε->void override;
	};

	//createResource / updateResource - every instance's resource sync (ResourceSyncAwait), which reaches the AppServer as the
	//instance's own login.  A known user who administers the row's schema (Authorize::TestSchemaAdmin) - on an update, the schema
	//it is in, any it moves to, and the row itself when it is enforced.  A schema with no enforced root passes, so a fresh install syncs.
	struct ResourceMAwait final : AdminWriteAwait{
		using AdminWriteAwait::AdminWriteAwait;
	private:
		α Test()ε->void override;
		α Execute()ι->TAwait<jvalue>::Task override;//reads the row's schema, and leaves a created row unenforced.
	};

	//createProvider / purgeProvider - a standalone OpcGateway's provider row for an OpcServer connection, run as the gateway's own
	//login (the hub's run as the system).  A known user with Administer on `users`:  a purge takes the provider's identities with it.
	struct ProviderMAwait final : AdminWriteAwait{
		using AdminWriteAwait::AdminWriteAwait;
	private:
		α Test()ε->void override;
	};
}

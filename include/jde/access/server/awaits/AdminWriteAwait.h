#pragma once
#include <jde/access/access.h>
#include <jde/ql/types/MutationQL.h>

//Generic writes the table's ops do not declare are the system's alone (GHSA-g354-grf2-r8vh).  These two have callers that are
//not the system, so Server::CustomMutation routes them here:  an admin check, then the stock mutation run as the system.
namespace Jde::Access::Server{
	//updatePermissionRight( id:42, allowed:255, denied:0 ) - the Permissions tab's save.  Administer on the grant's resource, the
	//right RoleMAwait::RemovePermission asks to drop it.
	struct PermissionRightMAwait final : TAwait<jvalue>, noncopyable{
		PermissionRightMAwait( QL::MutationQL m, UserPK executer, SRCE )ι:TAwait<jvalue>{ sl }, _mutation{ move(m) }, _executer{ executer }{}
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->TAwait<jvalue>::Task;
		QL::MutationQL _mutation;
		Jde::UserPK _executer;
	};

	//createResource / updateResource - every instance's resource sync (ResourceSyncAwait), which reaches the AppServer as the
	//instance's own login.  A known user who administers the row's schema (Authorize::TestSchemaAdmin) - on an update, the schema
	//it is in and any it moves to.  A schema with no enforced root passes, so a fresh install syncs.
	struct ResourceMAwait final : TAwait<jvalue>, noncopyable{
		ResourceMAwait( QL::MutationQL m, UserPK executer, SRCE )ι:TAwait<jvalue>{ sl }, _mutation{ move(m) }, _executer{ executer }{}
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->TAwait<jvalue>::Task;
		QL::MutationQL _mutation;
		Jde::UserPK _executer;
	};
}

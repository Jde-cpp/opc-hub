#pragma once
#include <jde/access/access.h>
#include <jde/ql/QLAwait.h>

namespace Jde::Access::Server{
	//groupRights( id: ){ resource{id schemaName slug criteria deleted} allowed denied effective sources{ permissionId allowed denied path{id type} } }
	//What a member gets through one group, from the server's cache (Authorize::GroupRights):  userRights' twin and row shape,
	//a system table emitted whole.  A path's groups run from the granted group to the one holding this one - empty = granted to it.
	//Gate:  Read on `acl`, as resourceRights has - a group has no "own row".
	struct GroupRightsAwait final : TAwait<jvalue>, noncopyable{
		GroupRightsAwait( const QL::TableQL& query, UserPK executer, SRCE )ι:
			TAwait<jvalue>{ sl },
			_query{ query },
			_executer{ executer }
		{}
		α Suspend()ι->void override;
	private:
		QL::TableQL _query;
		Jde::UserPK _executer;
	};
}
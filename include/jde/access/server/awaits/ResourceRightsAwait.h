#pragma once
#include <jde/access/access.h>
#include <jde/ql/QLAwait.h>

namespace Jde::Access::Server{
	//resourceRights( id: ){ resource{id schemaName slug criteria deleted} roles{id allowed denied sources{…}} groups{…} users{… effective …} }
	//where sources{ permissionId allowed denied path{id type} }.  Everyone a resource's grants reach, from the server's cache
	//(Authorize::Holders) - so a user's numbers are the ones Test and Rights enforce.  IdentityRightsAwait's twin:  a system table,
	//its fixed shape emitted whole, rights numeric, names left to the client.  A path is relative to its row - see Holders.
	//Gate:  Read on `acl`, the gate acl( identityId: ) and another user's userRights have.
	struct ResourceRightsAwait final : TAwait<jvalue>, noncopyable{
		ResourceRightsAwait( const QL::TableQL& query, UserPK executer, SRCE )ι:
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
#pragma once
#include "usings.h"
#include <jde/fwk/co/AnyAwait.h>

namespace Jde::Access{
	//A remote admin authorizer for one schema - the OpcServer, which alone knows which resource governs a node (the nearest
	//configured ancestor's: OpcAuthorize::TestAdminNode).  The AppServer registers an instance's socket for its schema on
	//kInstance, gated by Authorize::TestSchemaAdmin, and Authorize::TestAdminGrant( schema, … ) consults it.  Awaitable (AnyVoidAwait -
	//any coroutine can co_await it) because the answer round-trips a query to that client;  the local rule is Authorize's own
	//(TestAdminLocal) and does not implement this.
	struct IAdminAcl{
		β TestAdmin( str resource, str criteria, UserPK userPK, SRCE )ι->up<AnyVoidAwait> =0;
	};
	struct IAcl{
		β Test( str schemaName, str resourceName, ERights rights, UserPK executer, SRCE )ε->void=0;
		//A write the table's ops do not declare:  no resource row can grant it, and Test reads a missing row as not enforced and
		//passes anyone, so only the system passes.
		β TestSystem( str schemaName, str resourceName, ERights rights, UserPK executer, SRCE )ε->void=0;
		β TestUser( UserPK executer, SRCE )Ε->void=0;//a live user, or the system:  Unauthorized for one nobody knows, Forbidden for a deleted one.
		β Rights( str schemaName, str resourceName, UserPK executer )ι->ERights=0;
		β UserName( UserPK userPK )ι->string=0;
	};
}

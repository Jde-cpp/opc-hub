#pragma once
#include <jde/access/usings.h>

namespace Jde::DB{ struct AppSchema; struct Table; }

namespace Jde::Access{
	α FindRights( const jobject& o, sv key )ι->optional<ERights>;
	struct Permission final{
		Permission( const jobject& j )ε; //throws on a missing id - the one key the payload cannot do without; allowed/denied default to None as the mutation layer does.
		Permission( PermissionPK pk, Access::ResourcePK resourcePK, ERights Allowed, ERights Denied )ι;

		α Update( optional<ERights> allowed, optional<ERights> denied )ι->void;
		PermissionPK PK;
		Access::ResourcePK ResourcePK;
		ERights Allowed;
		ERights Denied;
	};
}

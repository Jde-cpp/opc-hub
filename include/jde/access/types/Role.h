#pragma once
#include <jde/access/usings.h>

namespace Jde::DB{ struct AppSchema; }
namespace Jde::Access{
	struct Role final{
		Role( RolePK rolePK, bool isDeleted  )ι:PK{rolePK}, IsDeleted{isDeleted}{}
		Role( const jobject& j )ι;
		RolePK PK;
		bool IsDeleted;
		flat_set<PermissionRole> Members;
	};
}
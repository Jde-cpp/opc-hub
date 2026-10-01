#pragma once
#include <jde/access/usings.h>
#include "Group.h"
#include "User.h"

namespace Jde::Access{
	struct Identities{
		flat_map<UserPK,User> Users;
		flat_map<GroupPK,Group> Groups;
	};
}
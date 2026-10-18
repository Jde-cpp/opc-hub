#pragma once
#include <jde/access/usings.h>
#include <jde/fwk/co/Await.h>
#include <jde/ql/QLAwait.h>
#include <jde/access/types/Role.h>

namespace Jde::QL{ struct IQL; }
namespace Jde::Access{
	struct RoleLoadAwait final : TAwait<flat_map<RolePK,Role>>{
		RoleLoadAwait( sp<QL::IQL> qlServer, UserPK executer )ι:_executer{executer}, _qlServer{qlServer}{};
	private:
		α Suspend()ι->void override{ Load();}
		α Load()ι->VoidTask;
		UserPK _executer;
		sp<QL::IQL> _qlServer;
	};
}
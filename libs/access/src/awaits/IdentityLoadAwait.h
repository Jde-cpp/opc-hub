#pragma once
#include <jde/access/usings.h>
#include <jde/ql/QLAwait.h>
#include <jde/access/types/Identities.h>

namespace Jde::QL{ struct IQL; }
namespace Jde::Access{
	struct IdentityLoadAwait final : TAwait<Identities>{
		IdentityLoadAwait( sp<QL::IQL> ql, UserPK executer, SRCE )ι:TAwait<Identities>{sl},_executer{executer},_ql{ql}{};
		α Suspend()ι->void override{ Load(); }
	private:
		α Load()ι->VoidTask;
		UserPK _executer;
		sp<QL::IQL> _ql;
	};
}
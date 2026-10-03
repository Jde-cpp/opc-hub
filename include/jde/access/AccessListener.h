#pragma once
#include <jde/ql/usings.h>
#include <jde/ql/IQL.h>
#include "usings.h"

namespace Jde::QL{ struct IQL; }
namespace Jde::Access{
	enum class ESubscription : uint16;
	struct Authorize;
	struct AccessListener final : QL::IListener, IShutdown{
		AccessListener( sp<QL::IQL> qlServer )ι:QL::IListener{"Access"},_qlServer{qlServer}{}
		α OnChange( const jvalue& j, QL::SubscriptionId clientId )ε->void override;
		α Shutdown( bool terminate, SL sl )ι->void override;
		sp<QL::IQL> _qlServer;
	private:
		α Authorizer()ι->Access::Authorize&{ return _qlServer->Authorizer(); }
		α AclChanged( ESubscription event, const jobject& o )ε->void;
		α UserChanged( UserPK userPK, ESubscription event, const jobject& o )ι->void;
		α GroupChanged( GroupPK groupPK, ESubscription event, const jobject& o )ε->void;
		α RoleChanged( RolePK rolePK, ESubscription event, const jobject& o )ε->void;
		α ResourceChanged( ResourcePK resourcePK, ESubscription event, const jobject& o )ε->void;
		α PermissionUpdated( PermissionRightsPK pk, const jobject& o )ε->void;
	};
}
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
		//For what rests on the rights and looks again when they change, a running flow's admission (the gateway's FlowHost):
		//`f` is called after each change a listener of this process has applied to its cache, on the subscription's thread, so
		//it only queues.  A change the cache refused, which OnChange throws, calls none.
		using Observer = function<void()>;
		Ω Observe( Observer f )ι->uint;//its handle, for Unobserve.
		Ω Unobserve( uint handle )ι->void;
		sp<QL::IQL> _qlServer;
	private:
		α Apply( const jvalue& j, QL::SubscriptionId clientId )ε->void;
		α Authorizer()ι->Access::Authorize&{ return _qlServer->Authorizer(); }
		α AclChanged( ESubscription event, const jobject& o )ε->void;
		α UserChanged( UserPK userPK, ESubscription event, const jobject& o )ι->void;
		α GroupChanged( GroupPK groupPK, ESubscription event, const jobject& o )ε->void;
		α RoleChanged( RolePK rolePK, ESubscription event, const jobject& o )ε->void;
		α ResourceChanged( ResourcePK resourcePK, ESubscription event, const jobject& o )ε->void;
		α PermissionUpdated( PermissionPK pk, const jobject& o )ε->void;
	};
}
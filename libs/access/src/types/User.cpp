#include <jde/access/types/User.h>

#define let const auto
namespace Jde::Access{
	constexpr ELogTags _tags{ ELogTags::Access };

	α User::UpdatePermission( PermissionPK permissionPK, optional<ERights> allowed, optional<ERights> denied )ε->void{
		auto permission = Permissions.find(permissionPK);
		if( permission==Permissions.end() )
			return;
		let resourcePK = permission->second.ResourcePK;
		permission->second.Update( allowed, denied );
		AllowedDisallowed rights{ ERights::None, ERights::None };
		for( let& [_,p] : Permissions ){
			if( p.ResourcePK==resourcePK ){
				rights.Allowed |= p.Allowed;
				rights.Denied |= p.Denied;
			}
		}
		Rights[resourcePK] = rights;
	}

	α User::operator+=( const Permission& permission )ι->User&{
		Permissions.insert_or_assign( permission.PK, permission );
		ASSERT( permission.ResourcePK );
		TRACE( "assigned user: {}, permission: {}, allowed: {}, denied: {}", PK.Value, permission.PK.Value, underlying(permission.Allowed), underlying(permission.Denied) );
		auto& rights = Rights[permission.ResourcePK];
		rights.Allowed |= permission.Allowed;
		rights.Denied |= permission.Denied;

		return *this;
	}
}
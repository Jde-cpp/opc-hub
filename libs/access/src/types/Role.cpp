#include <jde/access/types/Role.h>

#define let const auto

namespace Jde::Access{
	Ω getMembers( const jobject& j )ι->flat_set<PermissionRole>{
		flat_set<PermissionRole> members;
		if( auto p = Json::FindArray(j, "permissionRights"); p ){
			for( let& value : *p )
				members.emplace( PermissionPK{Json::AsNumber<PermissionPK::Type>(Json::AsObject(value), "id")} );
		}
		if( auto p = Json::FindArray(j, "roles"); p ){
			for( let& value : *p )
				members.emplace( RolePK{Json::AsNumber<RolePK::Type>(Json::AsObject(value), "id")} );
		}
		return members;
	}

	Role::Role( const jobject& j )ι:
		PK{ Json::FindNumber<RolePK::Type>(j, "id").value_or(0) },
		IsDeleted{ Json::FindTimePoint(j, "deleted").has_value() },
		Members{ getMembers(j) }
	{}
}
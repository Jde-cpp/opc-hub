#include <jde/access/types/Permission.h>

#define let const auto

namespace Jde::Access{
	Permission::Permission( PermissionPK pk, Access::ResourcePK resourcePK, ERights allowed, ERights denied )ι:
		PK{pk}, ResourcePK{resourcePK}, Allowed{allowed}, Denied{denied}
	{}

	//Missing is nullopt - None to the constructor, unchanged to an update:  RoleMAwait::AddPermission defaults an omitted allowed/denied
	//to 0, and a notification carries only the keys the mutation sent (Json::Combine of its args, TrimColumns copies what is present)
	//- so the key can be absent here, and this is noexcept:  AsNumber would have thrown across it into std::terminate (access-review3 #6).
	α FindRights( const jobject& o, sv key )ι->optional<ERights>{
		if( let array = Json::FindArray(o, key); array )
			return ToRights( *array );
		if( let number = Json::FindNumber<uint>(o, key); number )
			return (ERights)*number; //the same vocabulary as the names array, just not spelled out.
		return nullopt;
	}
	Permission::Permission( const jobject& o )ε:
		PK{ Json::AsNumber<PermissionPK::Type>(o, "id") },
		ResourcePK{ Json::FindNumberPath<Access::ResourcePK::Type>(o, "resource/id").value_or(0) },
		Allowed{ FindRights(o, "allowed").value_or(ERights::None) },
		Denied{ FindRights(o, "denied").value_or(ERights::None) }
	{}

	α Permission::Update( optional<ERights> allowed, optional<ERights> denied )ι->void{
		if( allowed )
			Allowed = *allowed;
		if( denied )
			Denied = *denied;
	}
}

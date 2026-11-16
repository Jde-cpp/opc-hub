#pragma once
#ifndef ACCESS_USINGS_H
#define ACCESS_USINGS_H
namespace Jde::Access{
	struct GroupPK final : PK<uint32>{};
	using ProviderPK=uint32;
	//A role is an access_permissions row too - roles share that pk space - but the two are never interchangeable here:  a
	//role's members are permissions or roles, and PermissionRole says which (access-refactor A3).
	struct PermissionPK final : PK<uint32>{};
	struct RolePK final : PK<uint32>{};
	struct ResourcePK final : PK<uint16>{};
	using PermissionRole=variant<PermissionPK,RolePK>;
	//An access event:  the row's kind and what became of it, the subscription's id (EventsSubscribeAwait) and the listener's switch.
	enum class ESubscription : uint16{
		Created=0x1, Updated=0x2, Deleted=0x4, Restored=0x8, Purged=0x10, Added=0x20, Removed=0x40,
		User=0x80, Group=0x100, Permission=0x200, Role=0x400, Resources=0x800, Acl=0x1000
	};
	enum class EProviderType : ProviderPK{
		None = 0,
		Google=1,
		Facebook=2,
		Amazon=3,
		Microsoft=4,
		VK=5,
		Key = 6,
		OpcServer = 7
	};

	enum class ERights : uint{ //one vocabulary for every resource, node ACLs included - OpcServer translates to UA masks (OpcAuthorize.h ToAccess), it does not store them here.
		None=0,
		Create=0x1,
		Read=0x2,
		Update=0x4,
		Delete=0x8,
		Purge=0x10,
		Administer=0x20,
		Subscribe=0x40,
		Execute=0x80,
		All = 0xFF
	};
	constexpr array<sv,9> RightsStrings = { "None", "Create", "Read", "Update", "Delete", "Purge", "Administer", "Subscribe", "Execute" };
	Ξ ToRight( sv x )ι->ERights{ return ToFlag<ERights>( RightsStrings, x ).value_or(ERights::None); }
	Ξ ToRights( const jarray& a )ι->Access::ERights{
		using enum Access::ERights;
		Access::ERights rights{ None };
		for( auto v : a )
			rights |= Access::ToRight( v.is_string() ? v.get_string() : "None" );
		return rights;
	}
	Ξ ToString( ERights r )ι->string{ return FromEnumFlag<ERights>( RightsStrings, r );  }
}
#endif
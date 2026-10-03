#pragma once
#include "usings.h"

namespace Jde::Access{
	struct IdentityPK{
		IdentityPK( Jde::UserPK pk )ι:Value{pk}{}
		IdentityPK( Access::GroupPK pk )ι:Value{pk}{}
		using Type=Jde::UserPK::Type;
		α IsUser()Ι->bool{ return Value.index()==0; }
		α UserPK()Ι->Jde::UserPK{ return get<Jde::UserPK>(Value); }
		α GroupPK()Ι->Access::GroupPK{ return get<Access::GroupPK>(Value); }
		α Underlying()Ι->Type{ return Value.index()==0 ? UserPK().Value : GroupPK().Value; }
		α operator!=( IdentityPK rhs )Ι->bool{ return Value!=rhs.Value; }
		α operator<( IdentityPK rhs )Ι->bool{ return Underlying() < rhs.Underlying(); }

		variant<Jde::UserPK,Access::GroupPK> Value;
	};
}
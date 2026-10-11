#pragma once
#include "../usings.h"

namespace Jde::Opc{
	//The rule of five for a wrapper that owns its vendor struct, over the vendor's generic copy/clear/init, so each wrapper
	//keeps only its converting constructors.  No members and no virtuals:  every wrapper stays layout-compatible with UA.
	//It is a complete base by the time a wrapper's constructor body runs, so a throw there still clears what the body allocated.
	template<class UA, uint16 TypeIndex>
	struct Owned : UA{
		Owned()ι:UA{}{}
		Owned( const UA& x )ι:UA{}{ UA_copy( &x, Raw(), Type() ); }
		Owned( UA&& x )ι:UA{ x }{ UA_init( &x, Type() ); }
		Owned( const Owned& x )ι:Owned{ static_cast<const UA&>(x) }{}
		Owned( Owned&& x )ι:Owned{ static_cast<UA&&>(x) }{}
		~Owned(){ UA_clear( Raw(), Type() ); }
		α operator=( const Owned& x )ι->Owned&{
			if( this!=&x ){
				UA_clear( Raw(), Type() );
				UA_copy( static_cast<const UA*>(&x), Raw(), Type() );
			}
			return *this;
		}
		α operator=( Owned&& x )ι->Owned&{
			if( this!=&x ){
				UA_clear( Raw(), Type() );
				*Raw() = x;
				UA_init( static_cast<UA*>(&x), Type() );
			}
			return *this;
		}
		//Hands the vendor struct over and leaves this empty.  `raw = move(wrapper)` cannot do that - it slices to the base,
		//whose implicit copy assignment is a shallow byte copy that leaves both sides owning the same memory.
		α Move()ι->UA{
			UA y = *this;
			UA_init( Raw(), Type() );
			return y;
		}
	private:
		α Raw()ι->UA*{ return this; }
		Ω Type()ι->const UA_DataType*{ return &UA_TYPES[TypeIndex]; }
	};
}
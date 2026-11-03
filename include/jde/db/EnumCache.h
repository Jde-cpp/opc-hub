#pragma once
#include <jde/fwk/io/Cache.h>
#include "exports.h"

#define Φ ΓDB α
namespace Jde::DB{
	struct Table;
	using EnumMap=flat_map<uint,string>;
	//The enum and flags tables' pk->name maps, keyed by table name:  SelectEnum answers from it, and a mutation of such a
	//table clears its entry.  Its default ttl is Forever, so an entry leaves only by that Clear.
	namespace EnumCache{
		constexpr steady_clock::duration Forever = Cache<string,EnumMap>::Forever;
		Φ Instance()ι->Cache<string,EnumMap>&;
		Φ Clear( const Table& table )ι->bool;
	}
}
#undef Φ
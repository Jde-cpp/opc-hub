#include <jde/db/EnumCache.h>
#include <jde/db/meta/Table.h>

namespace Jde::DB{
	α EnumCache::Instance()ι->Cache<string,EnumMap>&{
		static Cache<string,EnumMap> cache;//built on first use, once settings are loaded.
		return cache;
	}
	α EnumCache::Clear( const Table& table )ι->bool{
		return Instance().Clear( table.Name );
	}
}
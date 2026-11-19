#include "SqliteException.h"

namespace Jde::DB::Sqlite{
	α SqliteException::Throw()ε->void{ throw move(*this); }
}
#pragma once
#include <jde/access/usings.h>
#include <jde/access/access.h>

namespace Jde::DB{ struct AppSchema; struct IDataSource; struct Table; }
namespace Jde::Access{
	struct Authorize;
	α GetSchema()ι->DB::AppSchema&;
	α GetSchemaPtr()ι->sp<DB::AppSchema>;
	α SetSchema( sp<DB::AppSchema> schema )ι->void;
}
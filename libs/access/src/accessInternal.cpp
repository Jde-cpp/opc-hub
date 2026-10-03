#include "accessInternal.h"
#include <jde/db/meta/AppSchema.h>

namespace Jde::Access{
	static sp<DB::AppSchema> _schema;
	α GetSchemaPtr()ι->sp<DB::AppSchema>{ ASSERT(_schema); return _schema; }
	α GetSchema()ι->DB::AppSchema&{ return *GetSchemaPtr(); }
	α SetSchema( sp<DB::AppSchema> schema )ι->void{ /*ASSERT(!_schema);*/ _schema = schema; }
	α InstanceSchemaName( str schemaName, str opcServerInstance )ι->string{
		return opcServerInstance.empty() ? schemaName : Ƒ( "{}.{}", schemaName, opcServerInstance );
	}
}
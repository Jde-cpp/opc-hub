#include "opcInternal.h"
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>

#define let const auto

namespace Jde::Opc{
	static sp<DB::AppSchema> _schema;
	α Gateway::DS()ι->sp<DB::IDataSource>{ return _schema->DS(); }
	α Gateway::SetSchema( sp<DB::AppSchema> schema )ι->void{ _schema = schema; }
	α Gateway::GetViewPtr( sv name )ι->sp<DB::Table>{
		return _schema->GetViewPtr( string{name} );
	}
}
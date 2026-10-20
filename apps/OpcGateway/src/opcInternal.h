#pragma once

namespace Jde::DB{ struct AppSchema; struct IDataSource; struct Table; }
namespace Jde::Opc::Gateway{
	α DS()ι->sp<DB::IDataSource>;
	α SetSchema( sp<DB::AppSchema> schema )ι->void;
	α GetViewPtr( sv name )ι->sp<DB::Table>;
}
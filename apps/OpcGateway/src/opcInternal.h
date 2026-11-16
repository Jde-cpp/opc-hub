#pragma once

namespace Jde::Access{ enum class ERights : uint; }
namespace Jde::DB{ struct AppSchema; struct IDataSource; struct Table; }
namespace Jde::Opc::Gateway{
	α DS()ι->sp<DB::IDataSource>;
	α SetSchema( sp<DB::AppSchema> schema )ι->void;
	α GetViewPtr( sv name )ι->sp<DB::Table>;
	//Whether a stock mutation of `table` would pass the caller:  the table's right, as Table::Authorize tests it for the stock op,
	//which an unenforced resource passes.  A lack is an answer, not a denial to log;  a caller Authorize doesn't know is refused.
	α MayAuthorize( sv table, Access::ERights right, UserPK user, SL sl )ε->bool;
}
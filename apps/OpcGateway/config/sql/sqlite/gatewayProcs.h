#pragma once
#include <jde/db/sqlite_api.h>

struct sqlite3;

//Native twin of the server proc in the sibling mysql dir - same name, same param order, out param returned
//as the result row. RegisterProcs (jde/db/sqlite_api.h) registers it through the driver's IProcs.
namespace Jde::DB::Sqlite::GatewayProcs{
	α RegisterGatewayServerConnectionInsert( IProcs& procs )ι->void;
	//The historian's four tables (gateway_hist_insert.cpp):  twins of the inserts the other dialects generate.
	α RegisterGatewayHistTemplateInsert( IProcs& procs )ι->void;
	α RegisterGatewayHistTemplateNodeInsert( IProcs& procs )ι->void;
	α RegisterGatewayHistGroupInsert( IProcs& procs )ι->void;
	α RegisterGatewayHistGroupNodeInsert( IProcs& procs )ι->void;
}

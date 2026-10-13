#include <jde/db/sqlite_api.h>
#include "qlTestsProcs.h"

//Exported symbol the driver calls with its registry when Jde.QL.Tests' data source is sqlite - it dispatches Sql::IsProc
//calls to the twins registered here.
void RegisterProcs( Jde::DB::Sqlite::IProcs& procs ){
	Jde::DB::Sqlite::QLTestsProcs::RegisterQltItemInsert( procs );
	Jde::DB::Sqlite::QLTestsProcs::RegisterQltLabelInsert( procs );
}
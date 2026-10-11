#include <jde/db/sqlite_api.h>
#include "gatewayProcs.h"

//Exported symbol the driver calls with its registry when OpcGateway's datasource is sqlite - it dispatches
//Sql::IsProc calls to the twins registered here.
void RegisterProcs( Jde::DB::Sqlite::IProcs& procs ){
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayServerConnectionInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayHistTemplateInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayHistTemplateNodeInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayHistGroupInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayHistGroupNodeInsert( procs );
}

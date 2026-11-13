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
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeMachineInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeMachineSignalInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeReasonInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeCodingInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeMachinePurge( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeLineInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeLineMemberInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayOeeLinePurge( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayFlowInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayFlowVersionInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayFlowContextInsert( procs );
	Jde::DB::Sqlite::GatewayProcs::RegisterGatewayFlowRunInsert( procs );
}
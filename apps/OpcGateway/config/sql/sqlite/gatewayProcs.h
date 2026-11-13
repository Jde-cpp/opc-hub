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
	//OEE's four admin tables (gateway_oee_insert.cpp).
	α RegisterGatewayOeeMachineInsert( IProcs& procs )ι->void;
	α RegisterGatewayOeeMachineSignalInsert( IProcs& procs )ι->void;
	α RegisterGatewayOeeReasonInsert( IProcs& procs )ι->void;
	α RegisterGatewayOeeCodingInsert( IProcs& procs )ι->void;
	α RegisterGatewayOeeMachinePurge( IProcs& procs )ι->void;//gateway_oee_machine_purge.cpp.
	//The lines' two tables (gateway_oee_insert.cpp, gateway_oee_line_purge.cpp).
	α RegisterGatewayOeeLineInsert( IProcs& procs )ι->void;
	α RegisterGatewayOeeLineMemberInsert( IProcs& procs )ι->void;
	α RegisterGatewayOeeLinePurge( IProcs& procs )ι->void;
	//The flows' four tables (gateway_flow_insert.cpp, gateway_flow_purge.cpp).
	α RegisterGatewayFlowInsert( IProcs& procs )ι->void;
	α RegisterGatewayFlowVersionInsert( IProcs& procs )ι->void;
	α RegisterGatewayFlowContextInsert( IProcs& procs )ι->void;
	α RegisterGatewayFlowRunInsert( IProcs& procs )ι->void;
	α RegisterGatewayFlowPurge( IProcs& procs )ι->void;
}
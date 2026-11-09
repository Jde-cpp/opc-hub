#include "gatewayProcs.h"

//Twins of the *generated* insert procs of OEE's four admin tables (opcGateway-meta.jsonnet), as gateway_hist_insert.cpp's are:
//insertable columns in `i` order, created=$now, the sequence column out last.  oee_stops and oee_intervals have none:  the
//engine inserts them itself, and no QL create reaches them.
namespace Jde::DB::Sqlite{
	//	params: [0]=_hist_group_id, [1]=_calendar_id, [2]=_ideal_cycle, [3]=_count_unit, [4]=_small_stop, [5]=_idle_timeout,
	//	[6]=_width, [7]=_name, [8]=_slug, [9]=_attributes, [10]=_description; out _oee_machine_id.
	α GatewayProcs::RegisterGatewayOeeMachineInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_oee_machine_insert", "insert into gateway_oee_machines( hist_group_id, calendar_id, ideal_cycle, count_unit, small_stop, idle_timeout, width, name, slug, attributes, created, description ) values( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, unixepoch(), ? )", 11 );
	}
	//	params: [0]=_oee_machine_id, [1]=_role, [2]=_hist_group_node_id, [3]=_running, [4]=_rate_period; out _oee_machine_signal_id.
	α GatewayProcs::RegisterGatewayOeeMachineSignalInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_oee_machine_signal_insert", "insert into gateway_oee_machine_signals( oee_machine_id, role, hist_group_node_id, running, rate_period ) values( ?, ?, ?, ?, ? )", 5 );
	}
	//	params: [0]=_category, [1]=_code, [2]=_name, [3]=_slug, [4]=_attributes, [5]=_description; out _oee_reason_id.
	α GatewayProcs::RegisterGatewayOeeReasonInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_oee_reason_insert", "insert into gateway_oee_reasons( category, code, name, slug, attributes, created, description ) values( ?, ?, ?, ?, ?, unixepoch(), ? )", 6 );
	}
	//	params: [0]=_oee_machine_id, [1]=_start, [2]=_finish, [3]=_oee_reason_id, [4]=_comment, [5]=_identity_id, [6]=_user_name;
	//	out _oee_coding_id.
	α GatewayProcs::RegisterGatewayOeeCodingInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_oee_coding_insert", "insert into gateway_oee_codings( oee_machine_id, start, finish, oee_reason_id, comment, identity_id, user_name, created ) values( ?, ?, ?, ?, ?, ?, ?, unixepoch() )", 7 );
	}
}
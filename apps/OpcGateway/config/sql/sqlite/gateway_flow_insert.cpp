#include "gatewayProcs.h"

//Twins of the *generated* insert procs of the flows' four tables (opcGateway-meta.jsonnet), as gateway_hist_insert.cpp's are:
//insertable columns in `i` order, created=$now, the sequence column out last.  Only a flow's is called:  deployFlow, the host
//and a replay write the other three themselves, and no QL create reaches them.
namespace Jde::DB::Sqlite{
	//	params: [0]=_guid, [1]=_enabled, [2]=_trace, [3]=_name, [4]=_slug, [5]=_attributes, [6]=_description; out _flow_id.
	α GatewayProcs::RegisterGatewayFlowInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_flow_insert", "insert into gateway_flows( guid, enabled, trace, name, slug, attributes, created, description ) values( ?, ?, ?, ?, ?, ?, unixepoch(), ? )", 7 );
	}
	//	params: [0]=_flow_id, [1]=_version, [2]=_graph, [3]=_deployed, [4]=_identity_id, [5]=_user_name, [6]=_comment;
	//	out _flow_version_id.
	α GatewayProcs::RegisterGatewayFlowVersionInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_flow_version_insert", "insert into gateway_flow_versions( flow_id, version, graph, deployed, identity_id, user_name, comment ) values( ?, ?, ?, ?, ?, ?, ? )", 7 );
	}
	//	params: [0]=_flow_id, [1]=_node_id, [2]=_name, [3]=_value; out _flow_context_id.
	α GatewayProcs::RegisterGatewayFlowContextInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_flow_context_insert", "insert into gateway_flow_contexts( flow_id, node_id, name, value ) values( ?, ?, ?, ? )", 4 );
	}
	//	params: [0]=_flow_id, [1]=_version, [2]=_start, [3]=_finish, [4]=_mode, [5]=_started, [6]=_finished, [7]=_identity_id,
	//	[8]=_user_name, [9]=_status; out _flow_run_id.
	α GatewayProcs::RegisterGatewayFlowRunInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_flow_run_insert", "insert into gateway_flow_runs( flow_id, version, start, finish, mode, started, finished, identity_id, user_name, status ) values( ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )", 10 );
	}
}
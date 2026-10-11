#include "gatewayProcs.h"

//Twins of the *generated* insert procs of the historian's four tables (opcGateway-meta.jsonnet) - column order and
//defaults match TableDdl::InsertProcCreateStatement:  insertable columns in `i` order, created=$now, the sequence column
//out last, returned as the result row.
namespace Jde::DB::Sqlite{
	//	params: [0]=_name, [1]=_slug, [2]=_attributes, [3]=_description; out _hist_template_id.
	α GatewayProcs::RegisterGatewayHistTemplateInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_hist_template_insert", "insert into gateway_hist_templates( name, slug, attributes, created, description ) values( ?, ?, ?, unixepoch(), ? )", 4 );
	}
	//	params: [0]=_hist_template_id, [1]=_path, [2]=_exception_deviation, [3]=_exception_deviation_format, [4]=_min_time_interval,
	//	[5]=_max_time_interval, [6]=_stepped; out _hist_template_node_id.
	α GatewayProcs::RegisterGatewayHistTemplateNodeInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_hist_template_node_insert", "insert into gateway_hist_template_nodes( hist_template_id, path, exception_deviation, exception_deviation_format, min_time_interval, max_time_interval, stepped ) values( ?, ?, ?, ?, ?, ?, ? )", 7 );
	}
	//	params: [0]=_guid, [1]=_server_connection_id, [2]=_hist_template_id, [3]=_root, [4]=_exception_deviation,
	//	[5]=_exception_deviation_format, [6]=_min_time_interval, [7]=_max_time_interval, [8]=_stepped, [9]=_name, [10]=_slug,
	//	[11]=_attributes, [12]=_description; out _hist_group_id.
	α GatewayProcs::RegisterGatewayHistGroupInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_hist_group_insert", "insert into gateway_hist_groups( guid, server_connection_id, hist_template_id, root, exception_deviation, exception_deviation_format, min_time_interval, max_time_interval, stepped, name, slug, attributes, created, description ) values( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, unixepoch(), ? )", 13 );
	}
	//	params: [0]=_hist_group_id, [1]=_server_connection_id, [2]=_node, [3]=_hist_template_node_id, [4]=_exception_deviation,
	//	[5]=_exception_deviation_format, [6]=_min_time_interval, [7]=_max_time_interval, [8]=_stepped; out _hist_group_node_id.
	α GatewayProcs::RegisterGatewayHistGroupNodeInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "gateway_hist_group_node_insert", "insert into gateway_hist_group_nodes( hist_group_id, server_connection_id, node, hist_template_node_id, exception_deviation, exception_deviation_format, min_time_interval, max_time_interval, stepped ) values( ?, ?, ?, ?, ?, ?, ?, ?, ? )", 9 );
	}
}
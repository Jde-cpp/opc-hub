#include "gatewayProcs.h"

#define let const auto

//Twin of the proc half of ../mysql/gateway_server_connection_insert.sql - that file's trigger half is
//translated in gateway_server_connection_update.sql.
//	params: [0]=_name, [1]=_slug, [2]=_attributes, [3]=_description, [4]=_is_default, [5]=_default_browse_ns,
//	[6]=_certificate_uri, [7]=_url; out _server_connection_id returned as the result row.
namespace Jde::DB::Sqlite::GatewayProcs{
	α RegisterGatewayServerConnectionInsert( IProcs& procs )ι->void{
		procs.RegisterProc( "gateway_server_connection_insert", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
			if( !params[4].is_null() && params[4].get_number<uint>()!=0 )
				procs.ExecuteStatement( db, "update gateway_server_connections set is_default=0", {}, nullptr, sl );
			let y = procs.ExecuteStatement( db,
				"insert into gateway_server_connections( name, attributes, created, slug, description, certificate_uri, is_default, default_browse_ns, url )"
				" values( ?, ?, unixepoch(), ?, ?, ?, ?, ?, ? )", {params[0], params[2], params[1], params[3], params[6], params[4], params[5], params[7]}, nullptr, sl );
			if( onRow )
				(*onRow)( Row{ {Value{procs.LastInsertRowId(db)}} } ); //out _server_connection_id
			return y;
		}, 8 );
	}
}

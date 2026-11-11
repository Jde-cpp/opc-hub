#include "gatewayProcs.h"

//Twin of ../mysql/gateway_oee_line_purge.sql.
//	params: [0]=_oee_line_id; no out params.
namespace Jde::DB::Sqlite{
	α GatewayProcs::RegisterGatewayOeeLinePurge( IProcs& procs )ι->void{
		procs.RegisterProc( "gateway_oee_line_purge", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint{
			procs.ExecuteStatement( db, "savepoint oee_line_purge", {}, nullptr, sl );
			try{
				procs.ExecuteStatement( db, "delete from gateway_oee_line_members where oee_line_id=? or member_line_id=?", {params[0], params[0]}, nullptr, sl );
				const uint y = procs.ExecuteStatement( db, "delete from gateway_oee_lines where oee_line_id=?", {params[0]}, nullptr, sl );
				procs.ExecuteStatement( db, "release oee_line_purge", {}, nullptr, sl );
				return y;
			}
			catch( const std::exception& ){
				procs.ExecuteStatement( db, "rollback to oee_line_purge", {}, nullptr, sl );
				procs.ExecuteStatement( db, "release oee_line_purge", {}, nullptr, sl );
				throw;
			}
		}, 1 );
	}
}
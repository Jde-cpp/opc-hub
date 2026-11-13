#include "gatewayProcs.h"

//Twin of ../mysql/gateway_flow_purge.sql.
//	params: [0]=_flow_id; no out params.
namespace Jde::DB::Sqlite{
	α GatewayProcs::RegisterGatewayFlowPurge( IProcs& procs )ι->void{
		procs.RegisterProc( "gateway_flow_purge", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint{
			procs.ExecuteStatement( db, "savepoint flow_purge", {}, nullptr, sl );
			try{
				for( sv table : {"gateway_flow_contexts", "gateway_flow_runs", "gateway_flow_versions"} )
					procs.ExecuteStatement( db, Ƒ("delete from {} where flow_id=?", table), {params[0]}, nullptr, sl );
				const uint y = procs.ExecuteStatement( db, "delete from gateway_flows where flow_id=?", {params[0]}, nullptr, sl );
				procs.ExecuteStatement( db, "release flow_purge", {}, nullptr, sl );
				return y;
			}
			catch( const std::exception& ){
				procs.ExecuteStatement( db, "rollback to flow_purge", {}, nullptr, sl );
				procs.ExecuteStatement( db, "release flow_purge", {}, nullptr, sl );
				throw;
			}
		}, 1 );
	}
}
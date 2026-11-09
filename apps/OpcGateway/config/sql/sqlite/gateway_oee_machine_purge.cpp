#include "gatewayProcs.h"

//Twin of ../mysql/gateway_oee_machine_purge.sql.
//	params: [0]=_oee_machine_id; no out params.
namespace Jde::DB::Sqlite{
	α GatewayProcs::RegisterGatewayOeeMachinePurge( IProcs& procs )ι->void{
		procs.RegisterProc( "gateway_oee_machine_purge", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint{
			procs.ExecuteStatement( db, "savepoint oee_machine_purge", {}, nullptr, sl );
			try{
				for( sv table : {"gateway_oee_intervals", "gateway_oee_stops", "gateway_oee_codings", "gateway_oee_machine_signals"} )
					procs.ExecuteStatement( db, Ƒ("delete from {} where oee_machine_id=?", table), {params[0]}, nullptr, sl );
				const uint y = procs.ExecuteStatement( db, "delete from gateway_oee_machines where oee_machine_id=?", {params[0]}, nullptr, sl );
				procs.ExecuteStatement( db, "release oee_machine_purge", {}, nullptr, sl );
				return y;
			}
			catch( const std::exception& ){
				procs.ExecuteStatement( db, "rollback to oee_machine_purge", {}, nullptr, sl );
				procs.ExecuteStatement( db, "release oee_machine_purge", {}, nullptr, sl );
				throw;
			}
		}, 1 );
	}
}
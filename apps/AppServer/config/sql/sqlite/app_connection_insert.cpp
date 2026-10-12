#include "appProcs.h"

#define let const auto

//Twin of ../mysql/app_connection_insert.sql - no out params, the result row is (program_id, instance_id, connection_id).
//datetimes are stored as epoch ints (see the driver's SqliteRow), so now() becomes unixepoch().
//	params: [0]=_program_name, [1]=_instance_name, [2]=_host_name, [3]=_pid, [4]=_resource_schema.
namespace Jde::DB::Sqlite::AppProcs{
	α RegisterAppConnectionInsert( IProcs& procs )ι->void{
		procs.RegisterProc( "app_connection_insert", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
			auto programId = procs.ScalarUInt( db, "select program_id from app_programs where name=?", {params[0]}, sl );
			if( !programId )
				programId = ProgramInsert( procs, db, params[0], sl );
			auto instanceId = procs.ScalarUInt( db, "select instance_id from app_instances where program_id=? and name=?", {Value{*programId}, params[1]}, sl );
			if( !instanceId )
				instanceId = InstanceInsert( procs, db, Value{*programId}, params[1], params[2], sl );
			procs.ExecuteStatement( db, "update app_instances set resource_schema=? where instance_id=?", {params[4], Value{*instanceId}}, nullptr, sl );

			procs.ExecuteStatement( db, "update app_connections set deleted=unixepoch() where instance_id=? and deleted is null", {Value{*instanceId}}, nullptr, sl );
			let y = procs.ExecuteStatement( db, "insert into app_connections( instance_id, pid, created ) values( ?, ?, unixepoch() )", {Value{*instanceId}, params[3]}, nullptr, sl );
			if( onRow )
				(*onRow)( Row{ {Value{*programId}, Value{*instanceId}, Value{procs.LastInsertRowId(db)}} } );
			return y;
		}, 5);
	}
}

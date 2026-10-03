#include "accessProcs.h"

#define let const auto

//Twin of ../mysql/access_role_insert.sql.
//	params: [0]=_name, [1]=_slug, [2]=_attributes, [3]=_description; out _role_id returned as the result row.
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessRoleInsert( IProcs& procs )ι->void{
		procs.RegisterProc( "access_role_insert", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
			procs.ExecuteStatement( db, "insert into access_permissions( is_role ) values( ? )", {Value{true}}, nullptr, sl );
			let roleId = procs.LastInsertRowId( db );
			let y = procs.ExecuteStatement( db, "insert into access_roles( role_id, name, slug, attributes, description ) values( ?, ?, ?, ?, ? )", {Value{roleId}, params[0], params[1], params[2], params[3]}, nullptr, sl );
			if( onRow )
				(*onRow)( Row{ {Value{roleId}} } ); //out _role_id
			return y;
		}, 4 );
	}
}

#include "accessProcs.h"

//Twin of ../mysql/access_user_insert.sql.
//	params: [0]=_identity_id, [1]=_login_name, [2]=_password, [3]=_modulus, [4]=_exponent; no out params.
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessUserInsert( IProcs& procs )ι->void{
		procs.RegisterProc( "access_user_insert", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint{
			return procs.ExecuteStatement( db, "insert into access_users( identity_id, login_name, password, modulus, exponent ) values( ?, ?, ?, ?, ? )", {params[0], params[1], params[2], params[3], params[4]}, nullptr, sl );
		}, 5 );
	}
}

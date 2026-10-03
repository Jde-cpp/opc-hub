#include "accessProcs.h"

#define let const auto

//Twin of ../mysql/access_user_insert_login.sql - the generated access_identity_insert `call` is inlined via IdentityInsert.
//	params: [0]=_login_name, [1]=_provider_id, [2]=_provider_slug; out _identity_id returned as the result row.
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessUserInsertLogin( IProcs& procs )ι->void{
		procs.RegisterProc( "access_user_insert_login", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
			Value providerId = params[1];
			optional<string> providerName;
			if( !params[2].is_null() ){
				if( let id = procs.ScalarUInt(db, "select provider_id from access_providers where slug=?", {params[2]}, sl); id ) //mysql `select into` keeps _provider_id when no row matches.
					providerId = Value{*id};
				providerName = params[2].get_string();
			}
			else if( !params[1].is_null() ){
				RowΛ nameRow = [&providerName]( Row&& r ){ if( !r.IsNull(0) ) providerName = r.GetString(0); };
				procs.ExecuteStatement( db, "select name from access_provider_types where provider_type_id=?", {params[1]}, &nameRow, sl );
			}
			let providerSlug = providerName ? *providerName+"-"+params[0].get_string() : params[0].get_string();
			let email = providerName && *providerName=="Google" ? params[0] : Value{};//google login_name is the email; opcServer names are not.
			let identityId = IdentityInsert( procs, db, params[0], providerId, Value{providerSlug}, Value{}, Value{}, Value{false}, email, sl );
			let y = procs.ExecuteStatement( db, "insert into access_users( identity_id, login_name ) values( ?, ? )", {Value{identityId}, params[0]}, nullptr, sl );
			if( onRow )
				(*onRow)( Row{ {Value{identityId}} } ); //out _identity_id
			return y;
		}, 3 );
	}
}

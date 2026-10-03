#include "accessProcs.h"

#define let const auto

//Twin of ../mysql/access_ac_upsert_permission.sql.
//	params: [0]=_identityId, [1]=_allowed, [2]=_denied, [3]=_resourceId; out _permission_id returned as the result row.
//An upsert on (identity, resource):  resource_id already encodes the criteria, so the lookup asks nothing about it - the old
//`criteria is null` (a column only access_resources has) could never match a criteria-scoped resource, every re-grant minted a
//new permission, and the identity's rights became the OR of every grant ever made (access-review3 #12).
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessAcUpsertPermission( IProcs& procs )ι->void{
		procs.RegisterProc( "access_ac_upsert_permission", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
			auto permissionId = procs.ScalarUInt( db,
				"select max(permission_id) from access_acl join access_permission_rights using(permission_id)"
				" where resource_id=? and identity_id=?", {params[3], params[0]}, sl );
			uint y;
			if( !permissionId ){
				procs.ExecuteStatement( db, "insert into access_permissions( is_role ) values( ? )", {Value{false}}, nullptr, sl );
				permissionId = procs.LastInsertRowId( db );
				procs.ExecuteStatement( db, "insert into access_permission_rights( permission_id, resource_id, allowed, denied ) values( ?, ?, ?, ? )", {Value{*permissionId}, params[3], params[1], params[2]}, nullptr, sl );
				y = procs.ExecuteStatement( db, "insert into access_acl( identity_id, permission_id ) values( ?, ? )", {params[0], Value{*permissionId}}, nullptr, sl );
			}
			else
				y = procs.ExecuteStatement( db, "update access_permission_rights set allowed=?, denied=? where permission_id=?", {params[1], params[2], Value{*permissionId}}, nullptr, sl );
			if( onRow )
				(*onRow)( Row{ {Value{*permissionId}} } ); //out _permission_id
			return y;
		}, 4 );
	}
}

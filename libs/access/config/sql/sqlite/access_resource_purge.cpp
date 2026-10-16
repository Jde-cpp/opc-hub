#include "accessProcs.h"

#define let const auto

//Twin of ../mysql/access_resource_purge.sql.
//	params: [0]=_resource_id; no out params.
//The grants on the resource go with it (#357:  a hist group's own row, purged with the group by its gateway).  Each rights row
//on it names a permission access_role_add or access_ac_upsert_permission minted for that one resource, so the permission goes
//whole - its acl rows, its role memberships and the permission itself - once its rights row is gone.  A role is a permission
//too, but never holds rights of its own on a resource:  is_role=0 keeps one out of the list should a row ever say otherwise;
//its rights row still goes.  The members are collected first, as access_role_purge's are:  the rights rows must go before the
//permission rows can (fk), and would take the list with them.
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessResourcePurge( IProcs& procs )ι->void{
		procs.RegisterProc( "access_resource_purge", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint {
			vector<Value> members;
			RowΛ collect = [&members]( Row&& row ){ members.push_back( Value{row.Get<uint>(0)} ); };
			procs.ExecuteStatement( db,
				"select r.permission_id from access_permission_rights r join access_permissions p on p.permission_id=r.permission_id"
				" where r.resource_id=? and p.is_role=0", {params[0]}, &collect, sl );

			for( let& permissionId : members ){
				procs.ExecuteStatement( db, "delete from access_acl where permission_id=?", {permissionId}, nullptr, sl );
				procs.ExecuteStatement( db, "delete from access_role_members where member_id=?", {permissionId}, nullptr, sl );
			}
			procs.ExecuteStatement( db, "delete from access_permission_rights where resource_id=?", {params[0]}, nullptr, sl );
			for( let& permissionId : members )
				procs.ExecuteStatement( db, "delete from access_permissions where permission_id=?", {permissionId}, nullptr, sl );
			return procs.ExecuteStatement( db, "delete from access_resources where resource_id=?", {params[0]}, nullptr, sl );
		}, 1 );
	}
}
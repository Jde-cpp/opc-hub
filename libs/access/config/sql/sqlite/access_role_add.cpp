#include "accessProcs.h"

#define let const auto

//Twin of ../mysql/access_role_add.sql - mysql's null-safe `criteria <=> _criteria` becomes sqlite's `criteria is ?`.
//	params: [0]=_role_id, [1]=_allowed, [2]=_denied, [3]=_resourceSlug, [4]=_schema, [5]=_resourceName, [6]=_criteria;
//	out _permission_id returned as the result row.
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessRoleAdd( IProcs& procs )ι->void{
		procs.RegisterProc( "access_role_add", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
			auto resourceId = procs.ScalarUInt( db, "select resource_id from access_resources where slug=? and schema_name=coalesce(?, schema_name) and criteria is ?", {params[3], params[4], params[6]}, sl );
			if( !resourceId ){
				//install-issues #25: a *root* (criteria-null) resource that exists only because a role referenced it ships deleted,
				//i.e. unenforced.  The seed's `addRole(resource:{schemaName:"opc.install", slug:"nodeIds"})` runs before the
				//OpcServer connects and declares it, and creating it enforced (deleted null) refused the OpcServer's own delegation
				//and closed node access on a fresh install.  ResourceSyncAwait creates every shipped resource then deleteResource's
				//it, so unenforced is the shipped default; the grant applies once an operator enforces it.  A *criteria-scoped* row
				//is a deliberate per-node grant (the node-access page) and stays enforced - hence the `case`, not a flat deleted.
				procs.ExecuteStatement( db, "insert into access_resources( slug, schema_name, name, criteria, deleted ) values( ?, ?, coalesce(?, ?), ?, case when ? is null then unixepoch() else null end )", {params[3], params[4], params[5], params[3], params[6], params[6]}, nullptr, sl );
				resourceId = procs.LastInsertRowId( db );
			}
			auto permissionId = procs.ScalarUInt( db,
				"select permission_id from access_role_members members join access_permission_rights rights on members.member_id=rights.permission_id"
				" where members.role_id=? and rights.resource_id=?", {params[0], Value{*resourceId}}, sl );
			uint y;
			if( permissionId )
				y = procs.ExecuteStatement( db, "update access_permission_rights set allowed=?, denied=? where permission_id=?", {params[1], params[2], Value{*permissionId}}, nullptr, sl );
			else{
				procs.ExecuteStatement( db, "insert into access_permissions( is_role ) values( ? )", {Value{false}}, nullptr, sl );
				permissionId = procs.LastInsertRowId( db );
				procs.ExecuteStatement( db, "insert into access_permission_rights( permission_id, allowed, denied, resource_id ) values( ?, ?, ?, ? )", {Value{*permissionId}, params[1], params[2], Value{*resourceId}}, nullptr, sl );
				y = procs.ExecuteStatement( db, "insert into access_role_members( role_id, member_id ) values( ?, ? )", {params[0], Value{*permissionId}}, nullptr, sl );
			}
			if( onRow )
				(*onRow)( Row{ {Value{*permissionId}} } ); //out _permission_id
			return y;
		});
	}
}

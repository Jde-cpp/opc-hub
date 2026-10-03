#include "accessProcs.h"

//Twin of ../mysql/access_group_purge.sql.
//	params: [0]=_identity_id; no out params.
//groups' purgeProc, the shape of access_user_purge minus the access_users row a group never has:  access_acl grants the
//group its permissions and access_groups holds both its member list (identity_id=) and its own membership in parent
//groups (member_id=) - all three reference access_identities with no cascade, so PurgeAwait's `delete from
//access_identities` for the extended table failed on their fk and left the group behind.  No access_profiles delete:
//that fk points at access_users, so a group id can never appear there.  The identities delete below is what PurgeAwait
//then repeats as a no-op, same as the users twin.  Every statement is for a group only:  the id comes from purgeGroup, but
//users share access_identities, and this would otherwise take a user's acl rows (access-refactor B5).
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessGroupPurge( IProcs& procs )ι->void{
		procs.RegisterProc( "access_group_purge", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint{
			if( procs.ScalarUInt(db, "select 1 from access_identities where identity_id=? and is_group=1", {params[0]}, sl) ){
				procs.ExecuteStatement( db, "delete from access_acl where identity_id=?", {params[0]}, nullptr, sl );
				procs.ExecuteStatement( db, "delete from access_groups where identity_id=? or member_id=?", {params[0], params[0]}, nullptr, sl );
			}
			return procs.ExecuteStatement( db, "delete from access_identities where identity_id=? and is_group=1", {params[0]}, nullptr, sl );
		}, 1 );
	}
}

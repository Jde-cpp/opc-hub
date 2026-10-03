#include "accessProcs.h"

//Twin of ../mysql/access_user_purge.sql.
//	params: [0]=_identity_id; no out params.
//users' purgeProc:  access_profiles, access_acl and access_groups all reference the identity with no cascade, so a bare
//`delete from access_users` failed on the first of them - and with only acl/group rows it succeeded, then the identities
//delete failed, leaving an identity row nothing could purge (access-review3 #14).  PurgeAwait still follows with its own
//`delete from access_identities` for the extended table; that is a no-op after this.  For a user only, as the group twin is
//for a group (access-refactor B5).
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessUserPurge( IProcs& procs )ι->void{
		procs.RegisterProc( "access_user_purge", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint{
			if( procs.ScalarUInt(db, "select 1 from access_identities where identity_id=? and is_group=0", {params[0]}, sl) ){
				procs.ExecuteStatement( db, "delete from access_profiles where identity_id=?", {params[0]}, nullptr, sl );
				procs.ExecuteStatement( db, "delete from access_acl where identity_id=?", {params[0]}, nullptr, sl );
				procs.ExecuteStatement( db, "delete from access_groups where identity_id=? or member_id=?", {params[0], params[0]}, nullptr, sl );
				procs.ExecuteStatement( db, "delete from access_users where identity_id=?", {params[0]}, nullptr, sl );
			}
			return procs.ExecuteStatement( db, "delete from access_identities where identity_id=? and is_group=0", {params[0]}, nullptr, sl );
		}, 1 );
	}
}

#include "accessProcs.h"

//Twin of ../mysql/access_provider_purge.sql.
//	params: [0]=_provider_id; no out params.
//The identities' children go first - profiles, acl rows, group rows - or the users/identities deletes fail on their fk
//(access-review3 #14); the same set access_user_purge takes for one identity.
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessProviderPurge( IProcs& procs )ι->void{
		procs.RegisterProc( "access_provider_purge", [&procs]( sqlite3& db, const vector<Value>& params, RowΛ*, SL sl )->uint{
			procs.ExecuteStatement( db, "delete from access_profiles where identity_id in ( select identity_id from access_identities where provider_id=? )", {params[0]}, nullptr, sl );
			procs.ExecuteStatement( db, "delete from access_acl where identity_id in ( select identity_id from access_identities where provider_id=? )", {params[0]}, nullptr, sl );
			procs.ExecuteStatement( db, "delete from access_groups where identity_id in ( select identity_id from access_identities where provider_id=? ) or member_id in ( select identity_id from access_identities where provider_id=? )", {params[0], params[0]}, nullptr, sl );
			procs.ExecuteStatement( db, "delete from access_users where identity_id in ( select identity_id from access_identities where provider_id=? )", {params[0]}, nullptr, sl );
			procs.ExecuteStatement( db, "delete from access_identities where provider_id=?", {params[0]}, nullptr, sl );
			return procs.ExecuteStatement( db, "delete from access_providers where provider_id=?", {params[0]}, nullptr, sl );
		}, 1 );
	}
}

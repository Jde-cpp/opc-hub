#pragma once
#include <jde/db/sqlite_api.h>

struct sqlite3;

//Native twins of the server procs in the sibling mysql/sqlServer dirs - one <proc>.cpp per proc, same names,
//same param order, out params returned as the result row. Registered alongside the AppServer procs by
//RegisterProcs (jde/db/sqlite_api.h) in apps/AppServer/config/sql/sqlite, through the driver's IProcs.
namespace Jde::DB::Sqlite::AccessProcs{
	α RegisterAccessAcInsertRole( IProcs& procs )ι->void;
	α RegisterAccessAcUpsertPermission( IProcs& procs )ι->void;
	α RegisterAccessGroupPurge( IProcs& procs )ι->void;
	α RegisterAccessIdentityInsert( IProcs& procs )ι->void;
	α RegisterAccessPermissionInsert( IProcs& procs )ι->void;
	α RegisterAccessProviderInsert( IProcs& procs )ι->void;
	α RegisterAccessProviderPurge( IProcs& procs )ι->void;
	α RegisterAccessResourceInsert( IProcs& procs )ι->void;
	α RegisterAccessResourcePurge( IProcs& procs )ι->void;
	α RegisterAccessRoleAdd( IProcs& procs )ι->void;
	α RegisterAccessRoleInsert( IProcs& procs )ι->void;
	α RegisterAccessRolePurge( IProcs& procs )ι->void;
	α RegisterAccessRoleRemove( IProcs& procs )ι->void;
	α RegisterAccessUserInsert( IProcs& procs )ι->void;
	α RegisterAccessUserInsertKey( IProcs& procs )ι->void;
	α RegisterAccessUserInsertLogin( IProcs& procs )ι->void;
	α RegisterAccessUserPurge( IProcs& procs )ι->void;

	//Body of access_identity_insert, shared with the user_insert procs that `call` it in mysql - sqlite has no
	//procs (Syntax::HasProcs false), so they call it directly instead.  Defined in access_identity_insert.cpp.
	α IdentityInsert( IProcs& procs, sqlite3& db, const Value& name, const Value& providerId, const Value& slug, const Value& attributes, const Value& description, const Value& isGroup, const Value& email, SL sl )ε->uint; //returns new identity_id.
}

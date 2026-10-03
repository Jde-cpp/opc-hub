#pragma once
#include "exports.h"
#include <jde/db/sqlite_api.h> //ProcΛ, IProcs

struct sqlite3;

namespace Jde::DB::Sqlite{
	//Driver-internal registry. The app-facing surface is IProcs (in <jde/db/sqlite_api.h>) - proc DLLs register and
	//run statements through the IProcs the driver hands them, so they needn't link this driver.
	//The data source intercepts Sql::IsProc statements and dispatches to FindProc's result inside a transaction.
	//owner: whoever is answerable for the entry, so UnregisterProcs can tell "my registration" from "somebody else's with
	//the same name".  Without it a second registrar overwriting a name made the *first* one's teardown erase the
	//survivor's entry, and the proc vanished while its dll was still loaded.  nullptr = anonymous, matches only itself.
	α RegisterProc( string name, ProcΛ proc, uint minParams=0, const void* owner=nullptr )ι->void; //minParams: see IProcs::RegisterProc.
	α UnregisterProcs( const vector<string>& names, const void* owner=nullptr )ι->void; //remove a dll's procs before it unloads (see SqliteApi) so their ProcΛ std::functions aren't destroyed after dlclose.  Only entries this owner still holds.
	α FindProc( sv name )ι->sp<const ProcΛ>; //nullptr if not registered.  By value: the caller runs the proc long after the lock is gone, and the map is vector-backed.
	α RegisteredProcNames()ι->vector<string>; //for SqliteServerMeta::LoadProcs - DDL sync treats registered procs as existing.

	//Helpers for proc bodies - prepare/bind/step plain statements against `db`.
	α ExecuteStatement( sqlite3& db, sv sql, const vector<Value>& params, RowΛ* onRow, SL sl )ε->uint;
	α ScalarUInt( sqlite3& db, sv sql, const vector<Value>& params, SL sl )ε->optional<uint>;
	α LastInsertRowId( sqlite3& db )ι->uint;

	//The IProcs handed to proc DLLs (see sqlite_api.h); forwards to the registry free functions above so a DLL
	//can register + run statements without linking this driver.
	struct ProcRegistry : IProcs{ //SqliteApi extends this to also track+unregister its dll's proc names.
		α RegisterProc( string name, ProcΛ proc, uint minParams )ι->void override{ Sqlite::RegisterProc( move(name), move(proc), minParams ); }
		α ExecuteStatement( sqlite3& db, sv sql, const vector<Value>& params, RowΛ* onRow, SL sl )ε->uint override{ return Sqlite::ExecuteStatement( db, sql, params, onRow, sl ); }
		α ScalarUInt( sqlite3& db, sv sql, const vector<Value>& params, SL sl )ε->optional<uint> override{ return Sqlite::ScalarUInt( db, sql, params, sl ); }
		α LastInsertRowId( sqlite3& db )Ι->uint override{ return Sqlite::LastInsertRowId( db ); }
	};
}

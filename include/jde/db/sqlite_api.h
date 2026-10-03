#pragma once
#include <jde/db/Row.h>
#include <jde/db/Value.h>
#include <jde/db/awaits/DBAwait.h> //RowΛ

struct sqlite3;

namespace Jde::DB::Sqlite{
	//A native twin of a server-side stored procedure (sqlite has no procs). Runs inside a transaction owned by the
	//caller; out params come back as a single result row, in declaration order. Returns rows affected.
	//Convention: a twin that writes exactly one row returns literal 1, *not* ExecuteStatement's sqlite3_changes() -
	//that counter is connection-global (the last completed DML on the connection, not necessarily the statement just
	//run), so it is only accidentally right; ExecuteStatement throws rather than reporting 0, so 1 is never a lie.
	//Twins whose row count is genuinely variable (access_role_add, access_ac_upsert_permission, …) return their
	//final ExecuteStatement result instead.
	using ProcΛ = std::function<uint( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )>;

	//Registry + statement helpers the driver (Jde.DB.Sqlite) hands to each proc DLL's RegisterProcs. A DLL registers
	//its twins and runs statements through this, so it needn't link the driver - only <jde/db/sqlite_api.h>+Jde.DB.
	struct IProcs{
		//minParams = the twin's declared parameter count, checked centrally at dispatch: bodies index params[N]
		//positionally and would otherwise read past the end of a short vector (a Value variant from uninitialized
		//memory - bad_variant_access or a segfault inside a dlopen'd .so).  `<`, not `!=`: callers append an out
		//placeholder the twins ignore, so extra trailing params are expected.  0 = unchecked, for a twin that takes none.  No
		//default:  an omitted count was no check at all, and seven twins had omitted it (access-refactor B3).
		β RegisterProc( string name, ProcΛ proc, uint minParams )ι->void =0;
		β ExecuteStatement( sqlite3& db, sv sql, const vector<Value>& params, RowΛ* onRow, SL sl )ε->uint =0; //rows affected
		β ScalarUInt( sqlite3& db, sv sql, const vector<Value>& params, SL sl )ε->optional<uint> =0;
		β LastInsertRowId( sqlite3& db )Ι->uint =0; //sqlite3_last_insert_rowid - here so proc DLLs needn't link their own sqlite3 copy.
	protected:
		virtual ~IProcs()=default;
	};

	//The shape every generated insert proc has: bind the first `paramCount` params to `sql`, then return the new
	//sequence value as the single out row.  Only a prefix is bound because callers append an out-param placeholder
	//that `sql` has no '?' for (see IProcs::RegisterProc) - passing `params` whole would over-bind and fail.
	//Twins that do more than one statement (app_instance_insert, access_user_insert_key) keep their own lambda.
	Ξ RegisterInsertProc( IProcs& procs, string name, string sql, uint paramCount )ι->void{
		procs.RegisterProc( move(name), [&procs, sql=move(sql), paramCount]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
			const vector<Value> args{ params.begin(), params.begin()+(ptrdiff_t)paramCount };
			procs.ExecuteStatement( db, sql, args, nullptr, sl );
			if( onRow )
				(*onRow)( Row{ {Value{procs.LastInsertRowId(db)}} } );
			return 1; //one row - see ProcΛ.
		}, paramCount );
	}
}

//A MODULE dll exports nothing by default on Windows; each proc DLL defines RegisterProcs and the driver
//GetProcAddress's it (no import side), so it is always dllexport - unlike ΓLITE there is no import half.  On non-MSVC
//the attribute is inert today (MODULEs aren't -fvisibility=hidden) but survives that flag if ever added.
#ifdef _MSC_VER
	#define JDE_SQLITE_PROC __declspec( dllexport )
#else
	#define JDE_SQLITE_PROC __attribute__(( visibility("default") ))
#endif

extern "C"{
	//Every proc DLL exports this single symbol. The driver dlopens the DLL and calls it with its registry when the
	//configured datasource is sqlite; the DLL registers all its native proc twins through `procs`.
	JDE_SQLITE_PROC void RegisterProcs( Jde::DB::Sqlite::IProcs& procs );
}

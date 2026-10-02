#include <sqlite3.h>
#include <absl/synchronization/mutex.h>
#include "SqliteProcs.h"
#include "SqliteRow.h" //Bind/ToRow
#include "SqliteException.h"
#include <jde/db/DBException.h>

#define let const auto

namespace Jde::DB::Sqlite{
	//sp, not the ProcΛ itself: FindProc's shared_lock dies at its return, and this map is vector-backed - any later
	//insert or erase moves every element, so a raw pointer into it would dangle for the whole run of the proc it names.
	//A caller holding the sp also survives a RegisterProc that replaces the same name out from under it.
	struct Registration{ const void* Owner; sp<const ProcΛ> Proc; }; //Owner: see the note on RegisterProc in the header.
	absl::Mutex _procsMutex;
	flat_map<string,Registration> _procs ABSL_GUARDED_BY(_procsMutex);

	α RegisterProc( string name, ProcΛ proc, uint minParams, const void* owner )ι->void{
		if( minParams ) //wrap once here: the dispatch path can't know what arity each twin expects.
			proc = [minParams, name, body=move(proc)]( sqlite3& db, const vector<Value>& params, RowΛ* onRow, SL sl )->uint{
				THROW_IFSL( params.size()<minParams, "Proc '{}' expects {} params, got {}.", name, minParams, params.size() );
				return body( db, params, onRow, sl );
			};
		ul _{ _procsMutex };
		//Loud, but not fatal and not first-wins: this is ι, called from a dll's RegisterProcs at load, so a throw would
		//terminate - and refusing would be as arbitrary as replacing.  Whichever twin ends up live, having two of them is
		//the bug, and the log is what says so.
		if( let existing = _procs.find(name); existing!=_procs.end() )
			CRITICAL( "Proc '{}' is registered twice{} - the second registration wins and the first is unreachable.", name, existing->second.Owner==owner ? " by the same registrar" : " by different registrars" );
		_procs[move(name)] = Registration{ owner, ms<const ProcΛ>(move(proc)) };
	}
	α UnregisterProcs( const vector<string>& names, const void* owner )ι->void{
		ul _{ _procsMutex };
		for( let& name : names ){
			//Only if it is still ours: a later registrar may have taken the name, and erasing then would strip a proc
			//whose dll is still loaded and still dispatching.
			if( let p = _procs.find(name); p!=_procs.end() && p->second.Owner==owner )
				_procs.erase( p );
		}
	}
	α FindProc( sv name )ι->sp<const ProcΛ>{
		rl _{ _procsMutex };
		let p = _procs.find( string{name} );
		return p==_procs.end() ? nullptr : p->second.Proc;
	}
	α RegisteredProcNames()ι->vector<string>{
		rl _{ _procsMutex };
		vector<string> names; names.reserve( _procs.size() );
		for( let& [name, _] : _procs )
			names.push_back( name );
		return names;
	}

	//prepare_v2 compiles ONE statement and reports where it stopped; passing nullptr for that tail dropped everything
	//after the first without a word - `insert…; insert…; this is garbage` prepared clean, stepped to SQLITE_DONE and
	//reported one row changed.  MySQL runs the whole text (multi_queries), so looping is what makes the two agree, and
	//the garbage now reaches prepare and throws.  Like MySQL, the statements run in order and a later failure does not
	//undo an earlier one - a caller that needs all-or-nothing needs a transaction.
	α ExecuteStatement( sqlite3& db, sv sql, const vector<Value>& params, RowΛ* onRow, SL sl )ε->uint{
		const char* tail{ sql.data() };
		let end{ sql.data()+sql.size() };
		//total_changes, not changes: sqlite3_changes describes the last INSERT/UPDATE/DELETE *on the connection*, so it
		//reads stale after a create/select and would be counted once per statement here.  The delta is exactly this call.
		let before = sqlite3_total_changes( &db );
		for( uint prepared{}; tail<end; ){
			let text{ tail };
			sqlite3_stmt* stmt{};
			//SqliteException rather than a bare Exception: the result code is what ToDbError maps, and it was dropped here.
			if( let rc = sqlite3_prepare_v2(&db, text, (int)(end-text), &stmt, &tail); rc!=SQLITE_OK )
				throw SqliteException{ sl, rc, Sql{string{sql}, params}, "prepare failed: {}", sqlite3_errmsg(&db) };
			if( !stmt ){
				if( tail<=text )
					break; //prepare consumed nothing and produced nothing - belt and braces, this loop must always progress.
				continue;  //trailing whitespace, a bare ';' or a comment: prepare succeeds and hands back no statement.
			}
			std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> cleanup{ stmt, &sqlite3_finalize };
			//Params belong to one statement - which `?` of which statement each would fill is not something a caller can
			//say - so multi-statement text has to be parameterless.  Asked by trial-preparing the tail rather than by
			//scanning it (a trailing comment is not a statement, and only prepare can tell the two apart) and asked
			//*before* the first step, so this misuse rejects the call instead of half-running it.
			if( !prepared++ && params.size() && tail<end ){
				sqlite3_stmt* next{}; const char* ignored{};
				let more = sqlite3_prepare_v2( &db, tail, (int)(end-tail), &next, &ignored )==SQLITE_OK && next;
				sqlite3_finalize( next );
				if( more )
					throw SqliteException{ sl, SQLITE_MISUSE, Sql{string{sql}, params}, "has more than one statement and {} params - params need a single statement.", params.size() };
			}
			Bind( *stmt, params, sl );
			int rc;
			while( (rc=sqlite3_step(stmt))==SQLITE_ROW ){
				if( onRow )
					(*onRow)( ToRow(*stmt) );
			}
			if( rc!=SQLITE_DONE )
				throw SqliteException{ sl, rc, Sql{string{sql}, params}, "step failed: {}", sqlite3_errmsg(&db) };
		}
		return (uint)(sqlite3_total_changes(&db)-before);
	}

	α ScalarUInt( sqlite3& db, sv sql, const vector<Value>& params, SL sl )ε->optional<uint>{
		optional<uint> y;
		RowΛ f = [&y]( Row&& row ){ if( !row.IsNull(0) ) y = row.Get<uint>(0); };
		ExecuteStatement( db, sql, params, &f, sl );
		return y;
	}

	α LastInsertRowId( sqlite3& db )ι->uint{
		return (uint)sqlite3_last_insert_rowid( &db );
	}
}
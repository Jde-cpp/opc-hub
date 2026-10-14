#include "SqliteDataSource.h"
#include "jde/fwk/exceptions/Exception.h"
#include "jde/fwk/process/dll.h"
#include <jde/db/DBException.h>
#include <jde/db/generators/Functions.h>
#include "SqliteException.h"
#include "SqliteProcs.h"
#include "SqliteQueryAwait.h"
#include "SqliteRow.h"
#include "SqliteServerMeta.h"
#include "../../../src/DBLog.h"

#define let const auto

namespace Jde::DB::Sqlite{
	//`p( ?, ? )` -> `p` - the bare form the generators emit; only the mysql and odbc drivers prepend their `call`.  Both
	//the dispatch in ExecuteProc and the error message in InsertSeqSyncUInt spell this, and they have to agree - the second
	//is naming the proc the first failed to find.
	α procName( const DB::Sql& sql )ι->sv{ return Str::RTrim( sv{sql.Text}.substr(0, sql.Text.find('(')) ); }

	//A loaded proc dll. Extends ProcRegistry so the dll registers through us; we record the names it registers and
	//unregister them in the destructor - before _dll unloads - so their ProcΛ std::functions (bodies in the dll)
	//aren't destroyed after dlclose (which would fault during the registry's static teardown at exit).
	class SqliteApi final : public ProcRegistry{
		DllHelper _dll;
		vector<string> _procNames;
	public:
		SqliteApi( fs::path path ): _dll{ move(path) }{
			decltype(RegisterProcs)* registerProcs{ _dll["RegisterProcs"] };
			registerProcs( *this );
		}
		~SqliteApi(){ UnregisterProcs( _procNames, this ); } //while _dll is still mapped (destroyed after this body).  `this`: only the entries still ours - see UnregisterProcs.

		α RegisterProc( string name, ProcΛ proc, uint minParams )ι->void override{
			_procNames.push_back( name );
			Sqlite::RegisterProc( move(name), move(proc), minParams, this ); //not ProcRegistry::, which registers anonymously - this dll owns what it registers.
		}
	};

	//One SqliteApi per dll, shared process-wide: the proc registry is global, so two data sources configured with the
	//same dll register the same names - a per-data-source SqliteApi would unregister them when either was destroyed,
	//stripping procs the survivor still dispatches. Weak entries so the dll still unloads with its last data source.
	DllApiCache<SqliteApi> _dllApis;

	SqliteDataSource::~SqliteDataSource(){
		if( _db )
			sqlite3_close_v2( _db );
	}

	α SqliteDataSource::SetConfig( const jobject& config )ε->void{
		for( auto&& [catalogName, vcatalog] : Json::AsObject(config, "catalogs") ){
			let& catalog = Json::AsObject( vcatalog );
			if( let path = Json::FindSV(catalog, "path") )
				_path = *path; //defaults to ':memory:' when no catalog supplies a path.
			if( let busyMs = Json::FindNumber<uint32>(catalog, "busyTimeoutMs") )
				_busyTimeoutMs = *busyMs;
			for( auto&& [dbSchemaName, dbSchema] : Json::AsObject(catalog, "schemas") ){
				if( dbSchemaName.starts_with('_') ) //internal schema, not a real db schema.
					continue;
				for( auto&& [appSchemaName, vappSchema] : Json::AsObject(dbSchema) ){
					//An explicit null says "this schema has no native procs" (the OpcServer's, since its address space moved
					//to NodeSet2 xml);  the key missing altogether is still the configuration error it always was.
					let jlib = Json::AsObject(vappSchema).if_contains( "dynamicLib" );
					THROW_IFX( !jlib, Exception(SRCE_CUR, {ELogLevel::Critical, ELogTags::App}, "No dynamicLib for {}.{}.{}", catalogName, dbSchemaName, appSchemaName) );
					if( jlib->is_null() )
						continue;
					let lib = Json::AsSV( *jlib );
					//weakly_canonical: _procDlls and DllApiCache both key on the path as written, so `…/lib/x.so` and
					//`…/lib/./x.so` would load the same dll twice and register every twin twice over.
					std::error_code ec;
					fs::path dynamicLib{ fs::weakly_canonical(fs::path{lib}, ec) };
					if( ec )
						dynamicLib = fs::path{ lib }; //unresolvable (a path that does not exist yet): use it as written and let the load report.
					if( !_procDlls.contains(dynamicLib) ){
						auto api = _dllApis.Get( dynamicLib ); //ctor loads the dll and registers its procs.
						_procDlls.emplace( move(dynamicLib), move(api) );
					}
				}
			}
		}
	}

	α SqliteDataSource::Connection( SL sl )ε->sqlite3&{
		if( !_db ){
			//SQLITE_OPEN_FULLMUTEX (serialized) as a backstop; _connMutex is the real serialization.
			let rc = sqlite3_open_v2( _path.c_str(), &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr );
			try{
				//inside the try: open_v2 allocates a handle even when it fails, so throwing past the cleanup below left _db set and every later call skipped the reopen and used the dead handle.
				THROW_IFX( rc, SqliteException(sl, rc, Sql{}, "sqlite3_open_v2('{}'): {}", _path, _db ? sqlite3_errmsg(_db) : sqlite3_errstr(rc)) );
				sqlite3_extended_result_codes( _db, 1 ); //ToDbError needs SQLITE_CONSTRAINT_UNIQUE etc; the base SQLITE_CONSTRAINT cannot tell unique from fk/not-null.
				//Without this sqlite's busy handler is absent and a locked db fails the statement immediately; there is no
				//retry on EDbError::Timeout anywhere above, so the caller just sees the error.  Harmless for ':memory:',
				//which no second connection can reach - it is set unconditionally so the two paths cannot drift.
				sqlite3_busy_timeout( _db, (int)_busyTimeoutMs );
				//exec succeeds even when fks are compiled out (the pragma no-ops) - read the setting back instead of trusting rc.
				ExecuteStatement( *_db, "pragma foreign_keys=on", {}, nullptr, sl );
				THROW_IFSL( ScalarUInt(*_db, "pragma foreign_keys", {}, sl).value_or(0)!=1, "Could not enable foreign_keys on '{}' - fks would go unenforced.", _path );
				if( _path!=":memory:" ){
					string mode; //journal_mode=wal reports the resulting mode - stays on the prior journal if wal can't be used (e.g. network fs).
					RowΛ f = [&mode]( Row&& r ){ mode = r.GetString(0); };
					ExecuteStatement( *_db, "pragma journal_mode=wal", {}, &f, sl );
					if( mode!="wal" )
						WARN( "('{}') journal_mode=wal not applied - using '{}'.", _path, mode );
				}
			}
			catch( ... ){ //don't cache a failed or half-configured connection - a retry would skip the open and the pragmas. close_v2 is a no-op on the null handle an OOM open leaves behind.
				sqlite3_close_v2( _db );
				_db = nullptr;
				throw;
			}
		}
		return *_db;
	}

	α SqliteDataSource::Disconnect()ε->void{
		lg l{ _connMutex };
		if( _db ){
			sqlite3_close_v2( _db );
			_db = nullptr;
		}
	}

	//"call app_instance_insert( ?, ?, ?, ? )" never reaches the server - there is none. Dispatch to the
	//native twin registered in SqliteProcs, wrapped in a transaction so multi-statement procs stay atomic.
	α SqliteDataSource::ExecuteProc( DB::Sql& sql, SL sl, Params& exeParams )ε->uint{
		let name = procName( sql );
		let proc = FindProc( name );
		THROW_IFSL( !proc, "No native proc registered for '{}'.", name );
		//Before `begin immediate`, so there is nothing to roll back: the slice below drops the trailing out placeholder,
		//and with no params at all `end()-1` is a transposed range - std::length_error on libc++, a debug-iterator abort
		//on the MS STL.  Either way it is a logic_error, which every catch( runtime_error& ) in the driver and the awaits
		//lets through.  MySql asserts the same precondition (MySqlDataSource.cpp); this throws it, and SQLITE_MISUSE maps
		//to EDbError::None - a caller fault, 500 - as it does for #40's placeholder-count check.
		if( exeParams.HasOut() && sql.Params.empty() )
			throw SqliteException{ sl, SQLITE_MISUSE, DB::Sql{sql}, "'{}' was called with an out value but no params - the out param is the last placeholder.", name };
		auto& db = Connection( sl );
		ExecuteStatement( db, "begin immediate", {}, nullptr, sl );
		try{
			//MySql passes out params as trailing placeholders; the native proc returns them as a row instead - drop them here.
			vector<Value> params{ sql.Params.begin(), sql.Params.end()-(exeParams.HasOut() ? 1 : 0) };
			let y = (*proc)( db, params, exeParams.Function, sl );
			ExecuteStatement( db, "commit", {}, nullptr, sl );
			return y;
		}
		catch( ... ){
			//sqlite auto-rolls-back on SQLITE_FULL/IOERR/NOMEM/INTERRUPT (and some BUSY), and `rollback` is then itself an
			//error - "cannot rollback - no transaction is active".  ExecuteStatement throws on that, which skipped the
			//`throw;` and handed the caller a Syntax error about a statement they never wrote, in place of the real one.
			//get_autocommit is the only way to ask whether the transaction is still there; the try/catch covers a rollback
			//that fails for any other reason.  Either way the original exception is what leaves this function.
			if( !sqlite3_get_autocommit(&db) ){
				try{
					ExecuteStatement( db, "rollback", {}, nullptr, sl );
				}
				catch( const std::exception& e ){
					ERR( "rollback after a failed '{}' failed: {}", name, e.what() );
				}
			}
			throw;
		}
	}

	α SqliteDataSource::WithConnection( function<void(sqlite3&)> f, SL sl )ε->void{
		lg _{ _connMutex };
		f( Connection(sl) );
	}

	α SqliteDataSource::Execute( Sql&& sql, SL sl, Params exeParams )ε->uint{
		if( exeParams.Log )
			DB::Log( sql, sl );
		if( sql.IsProc ){
			//The proc path keeps the callback inside the lock on purpose: its RowΛ carries the out-param row and has to
			//run within the transaction, so that a callback which throws still reaches ExecuteProc's rollback.
			lg _{ _connMutex };
			return ExecuteProc( sql, sl, exeParams );
		}
		//Rows are collected under the lock and handed to the caller's RowΛ after it is released.  _connMutex is a plain
		//std::mutex and Disconnect() takes it too, so a callback that queried this data source from inside the step loop
		//deadlocked against itself - where the same callback on MySQL simply takes another pooled session, because that
		//driver iterates result.rows() after execute with no lock held.  One virtual owes its callers one contract.
		vector<Row> rows;
		RowΛ collect = [&rows]( Row&& r ){ rows.push_back( move(r) ); };
		uint y;
		{
			lg _{ _connMutex };
			auto& db = Connection( sl );
			y = ExecuteStatement( db, sql.Text, sql.Params, exeParams.Function ? &collect : nullptr, sl );
			if( exeParams.Sequence )
				y = (uint)sqlite3_last_insert_rowid( &db );
		}
		if( exeParams.Function )
			for( auto& row : rows )
				(*exeParams.Function)( move(row) );
		return y;
	}


	α SqliteDataSource::InsertSeqSyncUInt( DB::InsertClause&& insert, SL sl )ε->uint{
		auto sql = insert.Move();
		//Plain inserts: last_insert_rowid covers it - no out param needed, unlike MySql.
		if( !sql.IsProc )
			return Execute( move(sql), sl, {.Sequence=true} );
		//Proc twins return the sequence as their out row - the native equivalent of the generated mysql proc's OUT
		//param - so capture that rather than last_insert_rowid: a multi-statement twin's *last* insert needn't be the
		//sequence table (app_instance_insert inserts app_hosts first).  Execute's Sequence path can't see it: it
		//returns ExecuteProc's rows-affected before reaching the last_insert_rowid line.
		optional<uint> sequence;
		RowΛ f = [&sequence]( Row&& r ){
			if( !sequence && r.Size() )
				sequence = r.Get<uint>( 0 );
		};
		let name = string{ procName(sql) }; //owned - sql is moved below.
		Execute( move(sql), sl, {.Function=&f} );
		THROW_IFSL( !sequence, "Proc '{}' returned no out row - its twin must emit the sequence column.", name );
		return *sequence;
	}


	α SqliteDataSource::Query( Sql&& sql, bool outParams, SL sl )ε->QueryAwait{
		return QueryAwait{ mu<SqliteQueryAwait>(shared_from_this(), move(sql), outParams, sl), sl };
	}

	α SqliteDataSource::AtSchema( sv schema, SL sl )ε->sp<IDataSource>{
		THROW_IFSL( schema!="main", "Sqlite schema '{}' not supported - only 'main'.", schema ); //TODO: ATTACH DATABASE if multiple schemas needed.
		return shared_from_this();
	}

	α SqliteDataSource::ServerMeta()ι->IServerMeta&{
		if( !_serverMeta )
			_serverMeta = mu<SqliteServerMeta>( *this );
		return *_serverMeta;
	}
}
Jde::DB::IDataSource* GetDataSource(){
	return new Jde::DB::Sqlite::SqliteDataSource();
}
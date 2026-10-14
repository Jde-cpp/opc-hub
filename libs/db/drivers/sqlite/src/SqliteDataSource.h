#pragma once
#include "exports.h"
#include "jde/db/sqlite_api.h"
#include "usings.h"
#include <jde/db/Value.h>
#include <jde/db/IDataSource.h>
#include "SqliteProcs.h"
#include "SqliteSyntax.h"

struct sqlite3;

extern "C" ΓLITE Jde::DB::IDataSource* GetDataSource();

namespace Jde::DB::Sqlite{
	struct SqliteServerMeta;
	class SqliteApi;
	struct SqliteDataSource final : IDataSource{
		~SqliteDataSource() override;
		α RequiresSync()ι->bool override{ return _path == ":memory:"; }
		α CompletesInline()Ι->bool override{ return true; } //in-process: Select runs the statement before it returns.
		α Query( Sql&& sql, bool outParams, SRCE )ε->QueryAwait override;

		α ServerMeta()ι->IServerMeta& override;
		α Syntax()ι->const DB::Syntax& override{ return SqliteSyntax::Instance(); }

		α AtSchema( sv schema, SRCE )ε->sp<IDataSource> override; //'main' only; ATTACH could emulate others.  AtCatalog: the base's no-catalogs answer.
		α SchemaNameConfig( SL=SRCE_CUR )ι->string override{ return "main"; }
		α SetConfig( const jobject& config )ε->void override;
		α Disconnect()ε->void override;
		α WithConnection( function<void(sqlite3&)> f, SRCE )ε->void; //f on the connection under _connMutex, so no other statement comes between its own.  f mustn't call back into this data source - the mutex isn't recursive.
	private:
		α Execute( Sql&& sql, SL sl, Params exeParams )ε->uint override; //C1: the one primitive; IDataSource implements the sync wrappers over it.
		α InsertSeqSyncUInt( DB::InsertClause&& insert, SL sl )ε->uint override;
		α Connection( SL sl )ε->sqlite3&; //lazy open.
		α ExecuteProc( DB::Sql& sql, SL sl, Params& exeParams )ε->uint; //dispatch to SqliteProcs registry inside a transaction.

		flat_map<fs::path,sp<SqliteApi>> _procDlls; //shared with other data sources configured with the same dll - see _dllApis in the .cpp.
		//Single connection, serialized by _connMutex: an in-memory db is per-connection, so a MySql-style
		//session pool would hand each caller its own empty database. For file-backed dbs a pool + WAL is an option.
		sqlite3* _db{};
		std::mutex _connMutex;
		string _path{ ":memory:" };
		//sqlite defaults to 0 - a concurrent writer makes the next statement fail with SQLITE_BUSY straight away.  Any
		//second connection on the same file (another service, the CLI, a second cluster) hits it; catalog `busyTimeoutMs`
		//overrides, 0 restores sqlite's own behaviour.
		uint32 _busyTimeoutMs{ 5000 };
		up<SqliteServerMeta> _serverMeta;
	};
}
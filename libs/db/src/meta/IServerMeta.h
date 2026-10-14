#pragma once
#include <jde/db/exports.h>
#include "../usings.h"

namespace Jde::DB{
	struct Cluster; struct ColumnDdl; struct ForeignKey; struct IDataSource;  struct Index; struct Procedure; struct AppSchema; struct SchemaDdl; struct Table; struct TableDdl; struct Value;

	struct ΓDB IServerMeta{
		//#52: a reference, not an sp.  The meta is a up<> member of the data source, so it strictly cannot outlive it -
		//and holding an sp made every data source that reached ServerMeta() own itself, so its destructor never ran and
		//sqlite never got its close/WAL-checkpoint (nor MySQL its session close).  A reference also cannot be rebound or
		//nulled, which is the invariant: this meta belongs to that data source for its whole life.
		IServerMeta( IDataSource& ds ):_ds{ds}{}

		β LoadTables( sv schemaName, sv tablePrefix )Ε->flat_map<string,sp<Table>> = 0;
		β LoadTable( str schemaName, str tableName, SRCE )Ε->sp<TableDdl> = 0;
		β LoadIndexes( sv schemaName, sv tablePrefix, sv tableName={} )Ε->vector<Index> = 0;
		β LoadForeignKeys( str schemaName )Ε->flat_map<string,ForeignKey> = 0;
		β LoadProcs( str schemaName )Ε->flat_map<string,Procedure> = 0;
		//#241: `dflt` - a configured `default:` - onto `column` of the existing `table`, which SchemaDdl::SyncTables found
		//spelled otherwise in the catalog.  Runs Syntax::SetDefault's batch; sqlite, which has none, overrides this
		//to rewrite the table's stored CREATE TABLE text (SqliteServerMeta).
		β SetDefault( const Table& table, const ColumnDdl& column, const Value& dflt )Ε->void;
	private:
		β ToType( sv typeName )Ι->EType=0;
	protected:
		IDataSource& _ds;
	};
}
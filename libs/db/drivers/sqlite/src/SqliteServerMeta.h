#pragma once
#include "../../../src/meta/IServerMeta.h"

namespace Jde::DB{
	struct IDataSource; struct ForeignKey; struct Index; struct Procedure; struct Table;
}

namespace Jde::DB::Sqlite{
	//Introspection via pragmas instead of information_schema:
	//	tables:       select name from sqlite_master where type='table'
	//	columns:      pragma table_info(<table>)
	//	indexes:      pragma index_list(<table>) + pragma index_info(<index>)
	//	foreign keys: pragma foreign_key_list(<table>)
	//	procs:        the SqliteProcs registry - report registered native procs so DDL sync doesn't recreate them.
	struct SqliteServerMeta final : IServerMeta{
		SqliteServerMeta( IDataSource& ds ):
			IServerMeta{ ds }
		{}
		α LoadTables( sv schemaName, sv tablePrefix )Ε->flat_map<string,sp<Table>> override;
		α LoadTable( str schemaName, str tableName, SRCE )Ε->sp<TableDdl> override;
		α ToType( sv name )Ι->EType override; //affinity mapping: integer/text/real/blob/numeric.
		α LoadIndexes( sv schemaName, sv tablePrefix, sv tableName={} )Ε->vector<Index> override;
		α LoadForeignKeys( str schemaName )Ε->flat_map<string,ForeignKey> override;
		α LoadProcs( str schemaName )Ε->flat_map<string,Procedure> override;
		α SetDefault( const Table& table, const ColumnDdl& column, const Value& dflt )Ε->void override; //#241: rewrites the stored create statement - see SqliteDdl.h.
	};
}

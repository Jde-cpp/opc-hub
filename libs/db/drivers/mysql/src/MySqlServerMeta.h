#pragma once
#include <mutex>
#include "../../../src/meta/IServerMeta.h"

namespace Jde::DB{
	struct IDataSource; struct ForeignKey; struct Index; struct Procedure; struct Syntax; struct Table;
}

namespace Jde::DB::MySql{
	α ToDbType( sv columnType )ι->EType;
	//#241: INFORMATION_SCHEMA.COLUMNS' COLUMN_DEFAULT in Syntax::DefaultLiteral's spelling: a string is reported bare and
	//gets its quotes back, a bit as `b'1'`/`b'0'` (or `1`/`0` on a tinyint(1)) and becomes BoolLiteral, and an expression
	//default (EXTRA has DEFAULT_GENERATED) is kept as is - except the now-expression, which the catalog spells
	//`CURRENT_TIMESTAMP` when declared as that keyword and `now()` when declared as the parenthesized expression
	//MySqlSyntax::SetDefault has to use; both read back as NowDefault.
	//MariaDB reports the ddl's own spelling instead - a literal quoted and escaped, `current_timestamp()` bare, a nullable
	//column's implicit null as `NULL` - and flags nothing generated, so its text is brought to MySQL's shape first.
	α MySqlDefault( sv text, bool generated, EType type, const Syntax& syntax, bool mariaDB=false )ι->string;

	struct MySqlServerMeta final : IServerMeta{
		MySqlServerMeta( IDataSource& ds ):
			IServerMeta{ ds }
		{}
		α LoadTables( sv schemaName, sv tablePrefix )Ε->flat_map<string,sp<Table>> override;
		α LoadTable( str schemaName, str tableName, SRCE )Ε->sp<TableDdl> override;
		α ToType( sv name )Ι->EType override;
		α LoadIndexes( sv schemaName, sv tablePrefix, sv tableName={} )Ε->vector<Index> override;
		α LoadForeignKeys( str schemaName )Ε->flat_map<string,ForeignKey> override;
		α LoadProcs( str schemaName )Ε->flat_map<string,Procedure> override;
		α IsMariaDB()Ε->bool; //from the server's version string, asked once.
	private:
		mutable std::once_flag _mariaDBOnce;
		mutable bool _mariaDB{};
	};
}
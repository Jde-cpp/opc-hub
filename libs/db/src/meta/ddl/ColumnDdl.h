#pragma once
#include <jde/db/meta/Column.h>

namespace Jde::DB{
	//A column as the server describes it (IServerMeta::LoadTables), for SchemaDdl to compare against the configured
	//one.  Every loader builds its columns through this one ctor (db-refactor A8), so the three dialects answer the same
	//shape: `defaultSql` is the catalog's default text brought to Syntax::DefaultLiteral's spelling (#241) - each catalog
	//reports its own, `((1))`, `b'1'`, an unquoted string - so each loader normalizes its own - and there is no ordinal,
	//the loaders' statements order by it.
	struct ΓDB ColumnDdl final: Column{
		ColumnDdl( sv name, sv defaultSql, bool isNullable, EType type, optional<uint> maxLength, bool isSequence, optional<uint8> skIndex, optional<uint> numericPrecision, optional<uint> numericScale, sv defaultName={} )ι;
		//#39: a bool in skIndex's place converts to an *engaged* optional{0} - "column 0 of the primary key", not "no
		//primary key" - and it does so silently, right after a bool parameter that is spelled the same way at the call
		//site.  Deleting the all-bool spelling makes that a compile error instead of a wrong answer.
		ColumnDdl( sv name, sv defaultSql, bool isNullable, EType type, optional<uint> maxLength, bool isSequence, bool skIndex, optional<uint> numericPrecision, optional<uint> numericScale, sv defaultName={} )ι = delete;
		~ColumnDdl()override;//out-of-line key function, as TableDdl's: anchors typeinfo/vtable to libJde.DB.so so SchemaDdl's dynamic_cast<ColumnDdl> works on a column a driver .so built.

		Ω CreateStatement( const Column& config )ε->string;
		Ω DataTypeString( const Column& config )ι->string;
		Ω DeclaresKey( const Column& config )ι->bool; //the column's identity syntax is the table's whole primary key (Syntax::IdentityIsPrimaryKey) - TableDdl then adds no constraint of its own.

		string DefaultSql;  //the default the catalog reports, in the dialect's ddl spelling minus outer parentheses; empty for none.  Column::Default stays unset on a loaded column - only the configuration has a typed one.
		string DefaultName; //the default constraint's name, where the dialect names them (SQL Server) - Syntax::SetDefault drops it before adding the new one.
	};
}
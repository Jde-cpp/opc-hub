#pragma once
#include <jde/db/meta/Column.h>

namespace Jde::DB{
	//A column as the server describes it (IServerMeta::LoadTables), for SchemaDdl to compare against the configured
	//one.  Every loader builds its columns through this one ctor (db-refactor A8), so the three dialects answer the same
	//shape: `dflt` is already parsed - the literal a catalog reports is dialect-spelled (`1`, `((1))`, `b'1'`), so each
	//loader reads its own and hands over a Value or nothing - and there is no ordinal, the loaders' statements order by it.
	struct ΓDB ColumnDdl final: Column{
		ColumnDdl( sv name, optional<Value> dflt, bool isNullable, EType type, optional<uint> maxLength, bool isSequence, optional<uint8> skIndex, optional<uint> numericPrecision, optional<uint> numericScale )ι;
		//#39: a bool in skIndex's place converts to an *engaged* optional{0} - "column 0 of the primary key", not "no
		//primary key" - and it does so silently, right after a bool parameter that is spelled the same way at the call
		//site.  Deleting the all-bool spelling makes that a compile error instead of a wrong answer.
		ColumnDdl( sv name, optional<Value> dflt, bool isNullable, EType type, optional<uint> maxLength, bool isSequence, bool skIndex, optional<uint> numericPrecision, optional<uint> numericScale )ι = delete;

		Ω CreateStatement( const Column& config )ε->string;
		Ω DataTypeString( const Column& config )ι->string;
		Ω DeclaresKey( const Column& config )ι->bool; //the column's identity syntax is the table's whole primary key (Syntax::IdentityIsPrimaryKey) - TableDdl then adds no constraint of its own.
	};
}
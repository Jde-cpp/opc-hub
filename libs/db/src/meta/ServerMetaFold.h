#pragma once
#include <jde/db/meta/Table.h>
#include "ddl/ColumnDdl.h"
#include "ddl/ForeignKey.h"
#include "ddl/Index.h"
#include "ddl/TableDdl.h"

//C13: the row folds MySqlServerMeta and MsSqlSchemaProc both run over an INFORMATION_SCHEMA-style result set - one row
//per constraint *column*, to be gathered into one entry per constraint.  They were byte-identical, and db-review2 #1 had
//to patch the same fold in both.  Row parsing stays in the callers: the two dialects read `unique` differently (GetInt vs
//GetBit) and compute primaryKey differently (`PRIMARY` vs `<table>_pk`).
//
//FoldForeignKeyRow is NOT for the sqlite loader.  It reads pragma_foreign_key_list, which is already one row per column
//and *may legitimately repeat a column* - `foreign key(a,a) references p(x,y)` yields `a` twice, and the dedupe below
//would collapse it to one.  FoldColumnRow, StripParens, FoldIndexRow and AttachIndexes have no such dedupe and all three loaders use them.
namespace Jde::DB{
	//One row of a column listing into its table: the table is created on first sight - as a TableDdl, which is what the
	//loaders answer and SchemaDdl dynamic_casts back to - and the column appended in row order.
	Ξ FoldColumnRow( flat_map<string,sp<Table>>& tables, str tableName, sp<ColumnDdl> column )ι->void{
		tables.emplace( tableName, ms<TableDdl>(tableName) ).first->second->Columns.push_back( move(column) );
	}
	//#241: a default's text minus the parentheses a dialect wraps it in - SQL Server spells a number `((1))` and an
	//expression `(getutcdate())`, and sqlite stores `(unixepoch())` but reads `unixepoch()` back - so a loader's
	//DefaultSql and SchemaDdl's spelling of the configured default compare on one footing.  Only a pair that encloses
	//the whole text goes: `(1)+(2)` keeps both.
	Ξ StripParens( sv text )ι->sv{
		for( ; text.size()>=2 && text.front()=='(' && text.back()==')'; text = text.substr(1, text.size()-2) ){
			uint depth{};
			for( uint i=0; i<text.size()-1; ++i ){ //the opening paren has to close at the last character, not before.
				if( text[i]=='(' )
					++depth;
				else if( text[i]==')' && --depth==0 )
					return text;
			}
		}
		return text;
	}

	Ξ FoldForeignKeyRow( flat_map<string,ForeignKey>& fks, str name, str fkTable, str column, str pkTable )ι->void{
		auto pExisting = fks.find( name );
		if( pExisting==fks.end() )
			fks.emplace( name, ForeignKey{name, fkTable, {column}, pkTable} );
		else if( auto& columns=pExisting->second.Columns; find(columns, column)==columns.end() )
			columns.push_back( column );//a constraint can't list the same column twice, so a repeat is a duplicated row - folding it in would break SyncFKs' Columns compare and re-create every fk each sync.
	}

	Ξ FoldIndexRow( vector<Index>& indexes, str tableName, str indexName, str columnName, bool unique, bool primaryKey )ι->void{
		auto pExisting = find_if( indexes, [&](const auto& index){ return index.Name==indexName && index.TableName==tableName; } );
		auto& columns = pExisting==indexes.end()
			? indexes.emplace_back( indexName, tableName, primaryKey, vector<string>{}, unique, false ).Columns //clustered: every loader hard-codes false - nothing reads it back on a loaded index.
			: pExisting->Columns;
		columns.push_back( columnName );
	}

	//The indexes a loader read, onto the tables it read (db-refactor A9): LoadIndexes answers them table-named, and a
	//TableDdl is what the tables map holds (FoldColumnRow), so this is the dynamic_cast every loader used to write.
	Ξ AttachIndexes( flat_map<string,sp<Table>>& tables, vector<Index>&& indexes )ι->void{
		for( auto& index : indexes ){
			if( auto pTable = tables.find( index.TableName ); pTable!=tables.end() )
				std::dynamic_pointer_cast<TableDdl>( pTable->second )->Indexes.push_back( move(index) );
		}
	}
}
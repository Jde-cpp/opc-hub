#include "MsSqlSchemaProc.h"
#include <jde/fwk/str.h>
#include <jde/db/Row.h>
#include <jde/db/meta/Table.h>
#include "../../../../src/meta/ServerMetaFold.h"
#include "../../../../src/meta/ddl/Procedure.h"
#include "MsSqlStatements.h"
#include "../OdbcDataSource.h"
#define let const auto

namespace Jde::DB::MsSql{
	//sys.default_constraints' definition: SQL Server wraps a numeric default in parens, `((100))`, a bit's as `((0))`/`((1))`.
	//Strip them all, then parse; an expression default (`((1)+(2))`) fails TryTo and is no default, which is fine.
	Ω msSqlDefault( sv dflt, EType type )ι->optional<Value>{
		optional<Value> y;
		if( dflt.empty() )
			return y;
		if( type==EType::Int ){
			sv v = dflt;
			while( v.size()>=2 && v.front()=='(' && v.back()==')' )
				v = v.substr( 1, v.size()-2 );
			if( let value = Str::TryTo<_int>(v); value )
				y = Value{ *value };
		}
		else if( type==EType::Bit )
			y = Value{ dflt!="((0))" };
		return y;
	}

	α MsSqlSchemaProc::LoadColumns( DB::Sql&& sql )Ε->flat_map<string,sp<Table>>{
		auto rows = _ds.Select( move(sql) );
		flat_map<string,sp<Table>> tables;
		for( auto&& row : rows ){ //ColumnSql's columns; [2] is column_id, which the statement orders by, and [8] a 0/1 pk-membership flag, not the key ordinal, so it is not an SKIndex.
			let type = ToType( row.GetString(5) );
			FoldColumnRow( tables, row.GetString(0), ms<ColumnDdl>(row.GetString(1), msSqlDefault(row.GetString(3), type), row.GetBit(4), type, row.GetOpt<_int>(6), row.Get<_int>(7)!=0, optional<uint8>{}, row.GetOpt<_int>(9), row.GetOpt<_int>(10)) );
		}
		return tables;
	}

	α MsSqlSchemaProc::LoadTables( sv schemaName, sv tablePrefix )Ε->flat_map<string,sp<Table>>{
		DB::Sql sql{ Sql::ColumnSql(tablePrefix.size()), {Value{string{schemaName}}} };
		if (tablePrefix.size())
			sql.Params.push_back( Value{string{tablePrefix}+'%'} );

		auto tables = LoadColumns( move(sql) );
		AttachIndexes( tables, LoadIndexes(schemaName, tablePrefix) );
		return tables;
	}
	α MsSqlSchemaProc::LoadTable( str schemaName, str tableName, SL sl )Ε->sp<TableDdl>{
		auto sql = DB::Sql{ Sql::ColumnSql(true), {Value{schemaName}, Value{tableName}} };
		auto tables = LoadColumns( move(sql) );
		THROW_IFSL( tables.size()!=1, "Table not found '{}.{}'. size={}", schemaName, tableName, tables.size() );
		auto dbTable = dynamic_pointer_cast<TableDdl>( tables.begin()->second );
		dbTable->Indexes = LoadIndexes( schemaName, {}, dbTable->Name );
		return dbTable;
	}

	α MsSqlSchemaProc::LoadIndexes( sv schemaName, sv tablePrefix, sv tableName )Ε->vector<Index>{
		let schema = schemaName.empty() ? "dbo"sv : schemaName;

		vector<Index> indexes;
		auto result = [&]( Row&& row ){
			uint i=0;
			let tableName = row.TakeString(i++); let indexName = row.TakeString(i++); let columnName = row.TakeString(i++); let unique = row.GetBit(i++)==0;

			FoldIndexRow( indexes, tableName, indexName, columnName, unique, indexName==Ƒ("{}_pk", tableName) );
		};

		vector<Value> values{ Value{string{schema}} };
		if( tableName.size() )
			values.push_back( Value{string{tableName}} );
		else if( tablePrefix.size() )
			values.push_back( Value{string{tablePrefix}+'%'} );
		let sql = Sql::IndexSql( tableName.size(), tablePrefix.size() );
		_ds.Select( {sql, values}, result );

		return indexes;
	}

	α MsSqlSchemaProc::LoadProcs( str schemaName )Ε->flat_map<string,Procedure>{
		flat_map<string,Procedure> values;
		auto fnctn = [&]( Row&& row ){
			let name = row.TakeString(0);
			values.try_emplace( name, Procedure{name, schemaName} );
		};
		_ds.Select( {Sql::ProcSql(true), {Value{schemaName}}}, fnctn );
		return values;
	}

	//sys.types.name is the bare lowercase name, so an exact match: sql server's own two spellings, then the common table
	//(B3; the old StartsWith on bigint/smallint/varchar/bit/binary/decimal matched nothing an exact compare does not).
	α MsSqlSchemaProc::ToType( sv typeName )Ι->EType{
		using enum EType;
		if( typeName=="sysname" )
			return VarWChar;
		if( typeName=="tinyint unsigned" )
			return UInt8;
		let type = FindCommonType( typeName );
		if( !type )
			WARNT( ELogTags::App, "Unknown datatype({}).  need to implement, no big deal if not our table.", typeName );
		return type.value_or( None );
	}

	α MsSqlSchemaProc::LoadForeignKeys( str schemaName )Ε->flat_map<string,ForeignKey>{
		flat_map<string,ForeignKey> fks;
		auto result = [&]( Row&& row ){
			uint i=0;
			let name = row.TakeString(i++); let fkTable = row.TakeString(i++); let column = row.TakeString(i++); let pkTable = row.TakeString(i++); //let pkColumn = row.GetString(i++); let ordinal = row.Get<uint>(i);
			FoldForeignKeyRow( fks, name, fkTable, column, pkTable );
		};
		_ds.Select( {Sql::ForeignKeySql(schemaName.size()), {Value{schemaName}}}, result );
		return fks;
	}
}
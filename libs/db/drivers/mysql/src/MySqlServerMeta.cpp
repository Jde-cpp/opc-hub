#include "MySqlServerMeta.h"
#include <jde/db/IDataSource.h>//was MySqlDataSource.h, which drags in boost.mysql - nothing here needs the connection, and the test target compiles this file in.
#include "MySqlStatements.h"
#include <jde/db/Row.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Syntax.h>
#include "../../../src/meta/ServerMetaFold.h"
#include "../../../src/meta/ddl/Procedure.h"

#define let const auto

namespace Jde::DB::MySql{
	Ω loadTables( IDataSource& ds, const MySqlServerMeta& meta, str schemaName, sv tablePrefix, bool like )ε->flat_map<string,sp<Table>>{
		flat_map<string,sp<Table>> tables;
		auto onRow = [&]( Row&& row ){ //ColumnSql's columns; [2] is ORDINAL_POSITION, which the statement orders by.
			let type = meta.ToType( row.GetString(5) );
			let isId = row.Get<_int>( 8 ); //ColumnSql's hard-coded `0 is_id`, so nullopt today - `isId!=0` once made it an engaged optional{0}, every column claiming to be pk column 0.
			FoldColumnRow( tables, row.GetString(0), ms<ColumnDdl>(row.GetString(1), BitDefault(row.GetString(3), type), row.GetString(4)!="NO", type, row.GetOpt<_int>(6), row.Get<_int>(7)!=0, isId ? optional<uint8>{(uint8)(isId-1)} : optional<uint8>{}, row.GetOpt<_int>(9), row.GetOpt<_int>(10)) );
		};
		let exact = !like && tablePrefix.size();//LoadTable wants an exact match - LIKE treats '_' in names like role_member as a wildcard.
		Sql sql{ Ddl::ColumnSql(tablePrefix, exact), {Value{schemaName}} };
		if( tablePrefix.size() )
			sql.Params.emplace_back( like ? string{tablePrefix}+'%' : string{tablePrefix} );
		ds.Select( move(sql), onRow );
		AttachIndexes( tables, meta.LoadIndexes(schemaName, tablePrefix) );
		return tables;
	}
	α MySqlServerMeta::LoadTables( sv schemaName, sv tablePrefix )Ε->flat_map<string,sp<Table>>{
		return loadTables( _ds, *this, string{schemaName}, tablePrefix, true );
	}
	α MySqlServerMeta::LoadTable( str schemaName, str tableName, SL sl )Ε->sp<TableDdl>{
		auto tables = loadTables( _ds, *this, string{schemaName}, tableName, false );
		THROW_IFSL( tables.empty(), "Table '{}' not found in schema '{}'.", tableName, schemaName );
		return std::dynamic_pointer_cast<TableDdl>(tables.begin()->second);
	}

	α MySqlServerMeta::LoadIndexes( sv schemaName, sv tablePrefix, sv tableName )Ε->vector<Index>{ //B2: the schema is the caller's, as LoadTables already had it - not the connection's.
		let schema{ string{schemaName} };

		vector<Index> indexes;
		auto onRow = [&]( Row&& row ){
			uint i=0;
			auto tableName = row.GetString(i++);
			let indexName = row.GetString(i++); let columnName = row.GetString(i++); let unique = row.Get<_int>(i++)==0;
			FoldIndexRow( indexes, tableName, indexName, columnName, unique, indexName=="PRIMARY" );
		};

		Sql sql{ Ddl::IndexSql(tablePrefix, tableName.size()), {Value{schema}} };
		if( tableName.size() )
			sql.Params.push_back( Value{string{tableName}} );
		else if( tablePrefix.size() )
			sql.Params.push_back( Value{string{tablePrefix}+'%'} );
		_ds.Select( move(sql), onRow );

		return indexes;
	}

	α MySqlServerMeta::LoadProcs( str schemaName )Ε->flat_map<string,Procedure>{
		let schema{ Value{schemaName} };
		flat_map<string,Procedure> values;
		auto fnctn = [&values]( Row&& row ){
			string name = row.TakeString(0);
			values.try_emplace( name, Procedure{name} );
		};
		_ds.Select( {Ddl::ProcSql(true), {schema}}, fnctn );
		return values;
	}

	α MySqlServerMeta::LoadForeignKeys( str schemaName )Ε->flat_map<string,ForeignKey>{
		let& schema = schemaName;
		flat_map<string,ForeignKey> fks;
		auto result = [&]( Row&& row ){
			uint i=0;
			let name = row.GetString(i++); let fkTable = row.GetString(i++); let column = row.GetString(i++); let pkTable = row.GetString(i++); //let pkColumn = row.GetString(i++); let ordinal = row.Get<uint>(i);
			FoldForeignKeyRow( fks, name, fkTable, column, pkTable );
		};
		_ds.Select( {Ddl::ForeignKeySql(true), {Value{schema},Value{schema}}}, result );
		return fks;
	}

	α ToDbType( sv columnType )ι->EType{
		using enum EType;
		let lower = Str::ToLower( columnType );
		const sv all{ lower };
		let isUnsigned = all.find( " unsigned" )!=sv::npos;
		let open = all.find( '(' );
		let base = all.substr( 0, std::min(open, all.find(' ')) );  //'double precision' -> 'double'; 'decimal(10,2)' -> 'decimal'.
		let width = open==sv::npos ? optional<uint>{} : Str::TryTo<uint>( all.substr(open+1, all.find_first_of(",)", open)-open-1) );

		//mysql's own: the unsigned/width-bearing integers, the two floats (REAL is a DOUBLE synonym unless REAL_AS_FLOAT is
		//set, and #30: FLOAT is the 4-byte one - the reverse of the common table, so they must be answered first), the
		//text/blob families; then the common table (B3) for the rest, including the T-SQL spellings mysql never returns.
		if( base=="timestamp" ) return DateTime;
		if( base=="date" ) return SmallDateTime;
		if( base=="double" || base=="real" ) return Float;
		if( base=="float" ) return SmallFloat;
		if( base=="bigint" ) return isUnsigned ? ULong : Long;
		if( base=="int" || base=="integer" || base=="mediumint" ) return isUnsigned ? UInt : Int;
		if( base=="smallint" ) return isUnsigned ? UInt16 : Int16;
		if( base=="tinyint" ) return width==1u ? Bit : isUnsigned ? UInt8 : Int8; //BOOL/BOOLEAN are aliases for tinyint(1).
		if( base=="tinytext" || base=="mediumtext" || base=="longtext" || base=="json" ) return Text;
		if( base=="blob" || base=="tinyblob" || base=="mediumblob" || base=="longblob" ) return Blob;
		return FindCommonType( base ).value_or( None ); //`binary` includes Guid columns: MySqlSyntax::GuidType() is binary(16).
	}

	α MySqlServerMeta::ToType( sv typeName )Ι->EType{
		let type = ToDbType( typeName );//pure, so SyntaxTests covers the classification directly; the WARN stays here.
		if( type==EType::None )
			WARNT( ELogTags::Sql, "Unknown datatype({}).  need to implement, ok if not our table.", typeName );//WARNT, not WARN: `_tags` comes from the driver's usings.h, which this file no longer pulls in - see the include note above.
		return type;
	}
}
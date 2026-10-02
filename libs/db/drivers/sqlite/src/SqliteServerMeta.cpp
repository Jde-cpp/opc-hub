#include <sqlite3.h>
#include <absl/strings/match.h>
#include "SqliteServerMeta.h"
#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/meta/Table.h>
#include "../../../src/meta/ServerMetaFold.h"
#include "../../../src/meta/ddl/Procedure.h"
#include "SqliteProcs.h"

#define let const auto

namespace Jde::DB::Sqlite{
	//'~' escape: '_' is a like wildcard and sqlite has no default escape char.
	constexpr sv userTableFilter{ "m.type='table' and m.name not like 'sqlite~_%' escape '~'" };
	constexpr sv userObjectFilter{ "m.type in ('table','view') and m.name not like 'sqlite~_%' escape '~'" };

	//declared type 'varchar(255)' / 'decimal(10,2)' -> base name + length/precision/scale.
	Ω parseDeclaredType( sv declared )ι->std::tuple<string,optional<uint>,optional<uint>,optional<uint>>{
		let open = declared.find( '(' );
		string base{ Str::Trim(string{declared.substr(0, open)}) };
		optional<uint> length, precision, scale;
		if( open!=sv::npos ){
			let close = declared.find( ')', open );
			let args = declared.substr( open+1, close==sv::npos ? sv::npos : close-open-1 );
			let comma = args.find( ',' );
			if( let first = Str::TryTo<uint>(comma==sv::npos ? args : args.substr(0, comma)); first ){
				length = precision = first;
				if( comma!=sv::npos )
					scale = Str::TryTo<uint>( args.substr(comma+1) );
			}
		}
		return { move(base), length, precision, scale };
	}

	Ω loadTables( IDataSource& ds, const SqliteServerMeta& meta, sv tableName, bool like )ε->flat_map<string,sp<Table>>{
		flat_map<string,sp<Table>> tables;
		auto onRow = [&]( Row&& row ){
			uint i=0;
			let table = row.GetString( i++ );
			let name = row.GetString( i++ );
			++i; //cid: the statement orders by it, ColumnDdl has no ordinal.
			let dflt = row.GetString( i++ );
			let isNullable = row.Get<_int>( i++ )==0;
			let declared = row.GetString( i++ );
			let pk = row.Get<uint>( i++ );
			let pkCount = row.Get<uint>( i++ );
			let [baseType, maxLength, precision, scale] = parseDeclaredType( declared );
			let type = meta.ToType( baseType );
			//rowid alias: only a single-column integer pk auto-assigns - see SqliteSyntax::ToString/CreatePrimaryKey.
			let isIdentity = pk==1 && pkCount==1 && absl::StrContainsIgnoreCase(baseType, "int");
			FoldColumnRow( tables, table, ms<ColumnDdl>(name, BitDefault(dflt, type), isNullable, type, maxLength, isIdentity, pk ? optional<uint8>((uint8)(pk-1)) : optional<uint8>{}, precision, scale) );
		};
		Sql sql{ Ƒ("select m.name, ti.name, ti.cid, coalesce(ti.dflt_value,''), ti.\"notnull\", ti.type, ti.pk,"
			"\n\t(select count(*) from pragma_table_info(m.name) p where p.pk>0)"
			"\nfrom sqlite_master m, pragma_table_info(m.name) ti"
			"\nwhere {}{}"
			"\norder by m.name, ti.cid", userObjectFilter, tableName.size() ? (like ? " and m.name like ?" : " and m.name=?") : "") };
		if( tableName.size() )
			sql.Params.emplace_back( like ? string{tableName}+'%' : string{tableName} );
		ds.Select( move(sql), onRow );
		AttachIndexes( tables, meta.LoadIndexes({}, like ? tableName : sv{}, like ? sv{} : tableName) );
		//pragma_index_list omits the rowid-alias single-integer pk, so synthesize the pk index from pragma_table_info's pk columns (skip when an origin='pk' auto-index already covered a composite/non-rowid pk). Lets SchemaDdl's dedup skip the unsupported 'alter table add constraint ... primary key'.
		for( auto&& [name, table] : tables ){
			auto& dbIndexes = std::dynamic_pointer_cast<TableDdl>( table )->Indexes;
			if( find_if(dbIndexes, [](let& x){ return x.PrimaryKey; })!=dbIndexes.end() )
				continue;
			flat_map<uint8,string> pkColumns; //keyed by SKIndex (pk-1) to order composite keys.
			for( let& c : table->Columns )
				if( c->SKIndex )
					pkColumns.emplace( *c->SKIndex, c->Name );
			if( pkColumns.empty() )
				continue;
			vector<string> columns;
			for( let& [_,col] : pkColumns )
				columns.push_back( col );
			dbIndexes.emplace_back( "pk", name, true, move(columns), true, optional<bool>{} );
		}
		return tables;
	}

	α SqliteServerMeta::LoadTables( sv /*schemaName - always 'main'*/, sv tablePrefix )Ε->flat_map<string,sp<Table>>{
		return loadTables( _ds, *this, tablePrefix, true );
	}
	α SqliteServerMeta::LoadTable( str /*schemaName*/, str tableName, SL sl )Ε->sp<TableDdl>{
		auto tables = loadTables( _ds, *this, tableName, false );
		THROW_IFSL( tables.empty(), "Table '{}' not found.", tableName );
		return std::dynamic_pointer_cast<TableDdl>( tables.begin()->second );
	}

	α SqliteServerMeta::LoadIndexes( sv /*schemaName - always 'main'*/, sv tablePrefix, sv tableName )Ε->vector<Index>{
		vector<Index> indexes;
		auto onRow = [&indexes]( Row&& row ){
			uint i=0;
			let table = row.GetString( i++ );
			let indexName = row.GetString( i++ );
			let columnName = row.GetString( i++ ); //empty for rowid/expression members.
			let unique = row.Get<_int>( i++ )==1;
			let primaryKey = row.GetString( i++ )=="pk"; //origin: 'c'=create index, 'u'=unique constraint, 'pk'.
			FoldIndexRow( indexes, table, indexName, columnName, unique, primaryKey );
		};
		string filter;
		Sql sql;
		if( tableName.size() ){
			filter = " and m.name=?";
			sql.Params.emplace_back( string{tableName} );
		}
		else if( tablePrefix.size() ){
			filter = " and m.name like ?";
			sql.Params.emplace_back( string{tablePrefix}+'%' );
		}
		sql.Text = Ƒ( "select m.name, il.name, coalesce(ii.name,''), il.\"unique\", il.origin"
			"\nfrom sqlite_master m, pragma_index_list(m.name) il, pragma_index_info(il.name) ii"
			"\nwhere {}{}"
			"\norder by m.name, il.seq, ii.seqno", userTableFilter, filter );
		_ds.Select( move(sql), onRow );
		return indexes;
	}

	α SqliteServerMeta::LoadForeignKeys( str /*schemaName*/ )Ε->flat_map<string,ForeignKey>{
		flat_map<string,ForeignKey> fks;
		string current; //pragma doesn't expose constraint names - synthesize '<table>_fk<id>'; SyncFKs matches on Table+Columns, not name.
		auto onRow = [&]( Row&& row ){
			uint i=0;
			let table = row.GetString( i++ );
			let id = row.Get<uint>( i++ );
			let pkTable = row.GetString( i++ );
			let column = row.GetString( i++ );
			let name = Ƒ( "{}_fk{}", table, id );
			auto pExisting = fks.find( name );
			if( pExisting==fks.end() )
				fks.emplace( name, ForeignKey{name, table, {column}, pkTable} );
			else
				pExisting->second.Columns.push_back( column );
		};
		_ds.Select( {Ƒ("select m.name, fk.id, fk.\"table\", fk.\"from\""
			"\nfrom sqlite_master m, pragma_foreign_key_list(m.name) fk"
			"\nwhere {}"
			"\norder by m.name, fk.id, fk.seq", userTableFilter)}, onRow );
		return fks;
	}

	α SqliteServerMeta::LoadProcs( str /*schemaName*/ )Ε->flat_map<string,Procedure>{
		//No server procs - report the native registry so DDL sync treats registered procs as existing.
		flat_map<string,Procedure> procs;
		for( auto& name : RegisteredProcNames() ){
			let key = name;
			procs.try_emplace( key, Procedure{move(name)} );
		}
		return procs;
	}

	//sqlite's own spellings - its integer aliases, the unsigned forms our ddl never emits but a hand-made table might, and
	//bare `blob`, which is our GuidType (SqliteSyntax; bytes columns declare varbinary/binary/image) - then the common
	//table (B3), then the affinity rules for a type created outside our ddl.
	constexpr std::array<std::pair<sv,EType>,17> SqliteTypeNames{{
		{"integer",EType::Long}, {"int8",EType::Long}, {"mediumint",EType::Int}, {"int unsigned",EType::UInt}, {"integer unsigned",EType::UInt},
		{"bigint unsigned",EType::ULong}, {"int2",EType::Int16}, {"tinyint unsigned",EType::UInt8}, {"timestamp",EType::DateTime},
		{"double",EType::Float}, {"double precision",EType::Float}, {"blob",EType::Guid}, {"guid",EType::Guid},
		{"character",EType::Char}, {"clob",EType::Text}, {"bool",EType::Bit}, {"boolean",EType::Bit}
	}};
	α SqliteServerMeta::ToType( sv name )Ι->EType{
		using enum EType;
		let [base, length, precision, scale] = parseDeclaredType( name );
		let typeName = Str::ToLower( base );
		if( auto p = find_if(SqliteTypeNames, [&](let& x){ return x.first==typeName; }); p!=SqliteTypeNames.end() )
			return p->second;
		if( let common = FindCommonType(typeName); common )
			return *common;
		//sqlite affinity rules for types created outside our ddl: https://sqlite.org/datatype3.html#determination_of_column_affinity
		auto type{ Numeric };
		if( typeName.find("int")!=string::npos )
			type = Long;
		else if( typeName.find("char")!=string::npos || typeName.find("clob")!=string::npos || typeName.find("text")!=string::npos )
			type = VarChar;
		else if( typeName.empty() || typeName.find("blob")!=string::npos )
			type = VarBinary;
		else if( typeName.find("real")!=string::npos || typeName.find("floa")!=string::npos || typeName.find("doub")!=string::npos )
			type = Float;
		WARN( "Unmapped declared type '{}' - using affinity fallback {}.", name, (uint)type );
		return type;
	}
}
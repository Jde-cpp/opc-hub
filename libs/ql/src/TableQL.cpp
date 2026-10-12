#include <jde/ql/types/TableQL.h>
#include <jde/ql/types/FilterQL.h>
#include <jde/db/Row.h>
#include <jde/db/Key.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/DBSchema.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include "qlInternal.h"

#define let const auto
namespace Jde::QL{
	α dbTable( string jName, const vector<sp<DB::AppSchema>>& schemas, bool system, SL sl )ε->sp<DB::Table>{
		let dbName = DB::Names::ToPlural( DB::Names::FromJson(move(jName)) );
		return system ? DB::AppSchema::FindView( schemas, dbName ) : DB::AppSchema::GetViewPtr( schemas, dbName, sl );
	}

	TableQL::TableQL( string jName, jobject args, sp<jobject> variables, const vector<sp<DB::AppSchema>>& schemas, bool system, SL sl )ε:
		Input{ move(args), move(variables) },
		JsonName{ jName },
		_dbTable{ dbTable(jName, schemas, system, sl) }
	{}

	α TableQL::AddColumn( sv jsonName )ι->bool{
		auto existing = FindColumn( jsonName );
		if( existing )
			return false;

		ASSERT( _dbTable );
		auto dbColumn = _dbTable->FindColumn( DB::Names::FromJson(jsonName) );
		Columns.push_back( ColumnQL{string{jsonName}, dbColumn} );
		return true;
	}

	α TableQL::AddFilter( const string& column, const jvalue& value )ι->void{
		auto destination = &Args;
		if( auto filter = Args.find("filter"); filter!=Args.end() ){
			if( auto o = filter->value().if_object(); o )
				destination = o;
		}
		( *destination )[column] = value;
	}

	//The name as asked, else its singular - a client may nest `providerType{…}` and a caller then ask for "providerTypes".
	α TableQL::FindTable( sv jsonPluralName )ι->TableQL*{
		auto p = find_if( Tables, [&](let& t){return t.JsonName==jsonPluralName;} );
		if( p==Tables.end() ){
			let singular = DB::Names::ToSingular( jsonPluralName );
			p = find_if( Tables, [&](let& t){return t.JsonName==singular;} );
		}
		return p==Tables.end() ? nullptr : &*p;
	}
	//Read on every table the query names.  A child whose table declares no Read - identities under groups, permissionRights
	//under roles - is its parent's data, so the parent's check governs it:  an unenforced parent is open to everyone.  Only a
	//root read of such a table falls to Table::Authorize's known-user rule.
	α TableQL::AuthorizeRead( UserPK executer, SL sl, bool child )Ε->void{
		if( let table = DBTable(); table && (!child || !empty(table->Operations & Access::ERights::Read)) )
			table->Authorize( Access::ERights::Read, executer, sl );
		for( let& t : Tables )
			t.AuthorizeRead( executer, sl, true );
	}
	α TableQL::ExtractTable( sv jsonPluralName )ι->optional<TableQL>{
		auto p = FindTable( jsonPluralName );
		if( !p )
			return {};
		optional<TableQL> y{ move(*p) };
		Tables.erase( Tables.begin()+(p-Tables.data()) );
		return y;
	}
	α TableQL::FindTable( sv jsonPluralName )Ι->const TableQL*{
		return const_cast<TableQL*>( this )->FindTable( jsonPluralName );
	}

	α TableQL::GetTable( sv jsonPluralName, SL sl )ε->TableQL&{
		TableQL* y = FindTable( jsonPluralName );
		THROW_IFSL( !y, "Could not find table '{}'.", jsonPluralName );
		return *y;
	}

	α ColumnQL::QLType( const DB::Column& column, SL sl )ε->string{
		string qlTypeName = "ID";
		if( !column.SKIndex ){
			switch( column.Type ){ using enum DB::EType;
			case Bit:
				qlTypeName = "Boolean";
				break;
			case Int16: case Int: case Int8: case Long:
				qlTypeName = "Int";
				break;
			case UInt16: case UInt: case ULong:
				qlTypeName = "UInt";
				break;
			case SmallFloat: case Float: case Decimal: case Numeric: case Money:
				qlTypeName = "Float";
				break;
			case Guid://not ID, which the web reads as a number:  its text, as ValueToJson renders it and DB::Value parses it back.
				qlTypeName = "String";
				break;
			case None: case Binary: case VarBinary: case Cursor: case RefCursor: case Image: case Blob: case TimeSpan:
				throw Exception{ sl, ELogLevel::Debug, "EType {} is not implemented.", (uint)column.Type };
			case VarWChar: case VarChar: case NText: case Text: case Uri:
				qlTypeName = "String";
				break;
			case WChar: case UInt8: case Char:
				qlTypeName = "Char";
				break;
			case DateTime: case SmallDateTime:
				qlTypeName = "DateTime";
				break;
			}
		}
		return qlTypeName;
	}
	α TableQL::ToJson( DB::Row& row, const vector<DB::Object>& dbColumns )Ι->jobject{
		jobject y;
		for( uint i=0; i<dbColumns.size() && i<row.Size(); ++i ){
			let col = std::get_if<DB::AliasCol>( &dbColumns[i] );
			if( col )
				SetResult( y, col->Column, move(row[i]) );
			else
				CRITICALT( ELogTags::QL, "Column {} is not an AliasCol.", i );
		}
		return y;
	}

	α TableQL::OrderBy()Ε->string{
		auto json = OrderByJson();
		if( json.size()==0 )
			return {};
		string sql;
		for( let& [jsonName, ascending] : json ){
			let column = QL::FilterColumn( *DBTable(), jsonName );//#20: the same resolution the filter uses - an enum display name orders by its id column.
			if( sql.size()>0 )
				sql += ", ";
			sql += column->FQName();
			if( !ascending )
				sql += " desc";
		}
		return sql;
	}

	α TableQL::SetResult( jobject& o, const sp<DB::Column> dbColumn, DB::Value&& value )Ι->void{
		ASSERT(dbColumn);
		for( let& c : Columns ){
			if( c.DBColumn==dbColumn ){
				o[dbColumn->IsPK() && !dbColumn->IsEnum() ? "id" : c.JsonName] = ValueToJson( move(value), &c );
				return;
			}
		}
		for( let& t : Tables ){
			if( !o.contains(t.JsonName) )
				o[t.JsonName] = jobject{};
			if( auto sub = o.at(t.JsonName).if_object(); sub )
				t.SetResult( *sub, dbColumn, move(value) );
			else
				WARNT( ELogTags::QL, "[{}.{}]a column and a sub-table share this name - the sub-table is not filled for this row.", JsonName, t.JsonName );
		}
	}
	α TableQL::TransformResult( jarray&& result )Ι->jvalue{
		jvalue v;
		if( IsPlural() )
			v = move( result );
		else if( result.empty() )
			v = jobject{};
		else if( auto o = result[0].if_object(); o )
			v = move( *o );
		else{
			DBGT( ELogTags::QL, "[{}]a singular table's first result is a {}, not an object - returning no row.", JsonName, Json::Kind(result[0].kind()) );
			v = jobject{};
		}
		return ReturnRaw ? move( v ) : jobject{ {ReturnName(), move(v)} };
	}
	α TableQL::TransformResult( jobject&& result )Ι->jobject{
		return ReturnRaw ? move( result ) : jobject{ {ReturnName(), move(result)} };
	}
	α TableQL::TransformResult( string&& result )Ι->jvalue{
		jvalue v{ move(result) };
		return ReturnRaw ? move( v ) : jobject{ {ReturnName(), move(v)} };
	}
	α TableQL::ToString()Ι->string{
		string y = JsonName;
		y.reserve( 64*(1+Tables.size()) );
		y += ArgString();
		y += '{';
		if( Columns.size() ){
			vector<string> cols;
			for_each( Columns, [&cols](let& c){cols.push_back(c.JsonName);} );
			y += Str::Join( cols, " " );
		}
		for( let& t : Tables ){
			if( t.Args.size() || t.Columns.size() || t.Tables.size() )
				y += ' '+t.ToString();
		}
		y += '}';
		return y;
	}
	α TableQL::TrimColumns( const jobject& fullOutput )Ι->jobject{
		jobject y;
		for( let& c : Columns ){
			if( auto p = fullOutput.if_contains(c.JsonName); p )
				y[c.JsonName] = *p;
		}
		for( let& t : Tables ){
			if( auto p = fullOutput.if_contains(t.JsonName); p ){
				if( auto o = p->if_object(); o )
					y[t.JsonName] = t.TrimColumns( *o );
				else
					WARNT( ELogTags::QL, "[{}.{}]expected an object for the sub-table, got a {} - omitted.", JsonName, t.JsonName, Json::Kind(p->kind()) );
			}
		}
		return y;
	}
}
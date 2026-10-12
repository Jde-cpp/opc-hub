#pragma once
#include <jde/fwk/io/json.h>
#include <jde/db/names.h>
#include <jde/db/generators/SelectClause.h>
#include "FilterQL.h"
#include "Input.h"
#define let const auto

namespace Jde::DB{ struct Column; struct Row; struct Key; };
namespace Jde::QL{
	struct ColumnQL final{
		Ω QLType( const DB::Column& db, SRCE )ε->string;

		string JsonName;
		mutable sp<DB::Column> DBColumn;
	};
	struct TableQL final : Input{
		TableQL( string jName, jobject args, sp<jobject> variables, const vector<sp<DB::AppSchema>>& schemas, bool system=false, SRCE )ε;

		α AddColumn( sv jsonName )ι->bool;
		α AddFilter( const string& column, const jvalue& value )ι->void;
		α AuthorizeRead( UserPK executer, SL sl, bool child=false )Ε->void;
		α DBTable()Ι->sp<DB::Table>{ return _dbTable && _dbTable->QLView ? _dbTable->QLView : _dbTable; }
		α SetDBTable( sp<DB::Table> x )ι->void{ _dbTable = move(x); }
		α DBTableName()Ι->str{ return DBTable() ? DBTable()->Name : Str::Empty(); }
		α DefaultResult()Ι->jvalue{ return IsPlural() ? jvalue{jarray{}} : jvalue{jobject{}}; }
		α EraseColumn( sv jsonName )ι->void{ Columns.erase( remove_if( Columns.begin(), Columns.end(), [&](let& c){return c.JsonName==jsonName;}), Columns.end() ); }
		α FindColumn( sv jsonName )Ι->const ColumnQL*{ auto p = find_if( Columns, [&](let& c){return c.JsonName==jsonName;}); return p==Columns.end() ? nullptr : &*p; }
		α FindTable( sv jsonPluralName )Ι->const TableQL*;
		α FindTable( sv jsonPluralName )ι->TableQL*;
		α ExtractTable( sv jsonPluralName )ι->optional<TableQL>;
		α GetTable( sv jsonPluralName, SRCE )ε->TableQL&;
		α IsPlural()Ι->bool{ return DB::Names::IsPlural(JsonName); }
		α JTableName()Ι->string override{ return JsonName; }
		α OrderBy()Ε->string;
		α SetResult( jobject& o, const sp<DB::Column> dbColumn, DB::Value&& value )Ι->void;
		α ReturnName()Ι->string{ return Alias.size() ? Alias : JsonName; }
		α TransformResult( jarray&& result )Ι->jvalue;
		α TransformResult( jobject&& result )Ι->jobject;
		α TransformResult( string&& result )Ι->jvalue;
		α ToJson( DB::Row& row, const vector<DB::Object>& dbColumns )Ι->jobject;
		α ToString()Ι->string;
		α TrimColumns( const jobject& fullOutput )Ι->jobject;

		string Alias;
		vector<ColumnQL> Columns;
		string JsonName;
		vector<TableQL> Tables;
		vector<TableQL> InlineFragments; //... on Type { }
		bool ReturnRaw{true};
	private:
		sp<DB::Table> _dbTable;
	};
}
#undef let
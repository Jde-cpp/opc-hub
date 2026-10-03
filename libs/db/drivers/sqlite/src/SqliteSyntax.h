#pragma once
#include <jde/db/generators/Syntax.h>

namespace Jde::DB::Sqlite{
	//Driver-local dialect - resolved dynamically through IDataSource::Syntax(), so nothing in Jde.DB needs to know about it.
	//Would have to move into include/jde/db/generators/Syntax.h (next to the base dialect) if DDL/generators ever
	//needed it statically; the mysql driver keeps its MySqlSyntax the same way.
	struct SqliteSyntax final : Syntax{
		Ω Instance()->const SqliteSyntax&{ static const SqliteSyntax _instance; return _instance; }
		α CanAddForeignKeys()Ι->bool override{ return false; } //no 'alter table add constraint' - fks only enforced when inline in create table.
		α CanSetDefaultSchema()Ι->bool override{ return false; } //single db per connection; ATTACH could emulate schemas.
		α CatalogSelect()Ι->sv override{ return {}; }
		α DateTimeSelect( sv columnName )Ι->string override{ return string{columnName}; } //stored as unix epoch integer - see SqliteRow.
		α QuoteChars()Ι->std::pair<char,char> override{ return {'"','"'}; }
		α GuidType()Ι->sv override{ return "blob"; }
		α HasLength( EType )Ι->bool override{ return false; } //type affinity - lengths are documentation only.
		α HasCatalogs()Ι->bool override{ return false; }
		α HasProcs()Ι->bool override{ return false; } //generated insert procs -> plain sql + last_insert_rowid; hand-written procs dispatch to SqliteProcs registry.
		//sqlite needs the conflict target named; `excluded` is the row the insert would have written.
		α UpsertSuffix( const vector<sv>& keyColumns, const vector<sv>& updateColumns )Ι->string override{
			return Ƒ( " on conflict({}) do update set {}", Str::Join(keyColumns, ","), SetList(updateColumns, "excluded.", "") );
		}
		α HasSchemas()Ι->bool override{ return false; }
		α HasUnsigned()Ι->bool override{ return false; }
		α IdentityColumnSyntax()Ι->sv override{ return "primary key autoincrement"; } //a bare rowid alias hands out max+1, so a deleted newest pk went to the next row; autoincrement never gives one out again.  Only valid as the column's own pk constraint - see IdentityIsPrimaryKey.
		α IdentityIsPrimaryKey()Ι->bool override{ return true; }
		α IdentitySelect()Ι->sv override{ return "last_insert_rowid()"; }
		α IndexName( sv tableName, sv indexName )Ι->string override{ return Ƒ("{}_{}", tableName, indexName); } //index names are schema-wide - qualify with the table (e.g. access_providers_nk).
		α CreatePrimaryKey( str /*tableName*/, str columns )Ι->string override{ return Ƒ("PRIMARY KEY( {} )", columns); } //columns: comma-separated for composite keys. Single-column integer pk stays a rowid alias.
		α Limit( str sql, uint limit, uint skip )Ι->string override{ return LimitOffset( sql, limit, skip, "-1" ); } //negative limit = no upper bound.
		α NeedsIdentityInsert()Ι->bool override{ return false; }
		α NowDefault()Ι->sv override{ return "(unixepoch())"; }
		//The only dialect that takes a `glob:` pattern verbatim - QL::globMatch *is* sqlite GLOB, so the in-memory filter
		//and the where clause agree character for character, negated classes and all.  REGEXP is a different story: sqlite
		//parses the operator but ships no implementation, and this driver registers no such udf (SqliteProcs is the C++
		//proc registry, not sqlite3_create_function), so `x regexp ?` would fail at execution with "no such function".
		//Refusing here turns that into a clear error at statement build instead.
		α PatternOperator( EOperator op, SRCE )Ε->sv override{
			if( op==EOperator::Glob )
				return "glob";
			throw Exception{ sl, Jde::ELogLevel::Debug, "sqlite has no '{}' operator - REGEXP needs a udf this driver does not register; use 'glob'.", DB::ToString(op) };
		}
		α PatternParam( EOperator op, str pattern, SRCE )Ε->string override{
			PatternOperator( op, sl );
			return pattern;
		}
		//No server-side procs - proc calls dispatch to native C++ registered in SqliteProcs.h, so Proc* generators are unused.
		α ProcParameterPrefix()Ι->sv override{ return {}; }
		α ProcStart()Ι->sv override{ return {}; }
		α SchemaExistsSql()Ι->sv override{ return "select name from pragma_database_list where name=?"; } //'main' always exists - schema creation is a no-op.
		α SchemaSelect()Ι->sv override{ return {}; } //no schemas - SchemaName falls back to SysSchema ('main').
		α SpecifyIndexCluster()Ι->bool override{ return false; }
		α SysSchema()Ι->sv override{ return "main"; }
		//rowid alias requires the declared type be exactly 'integer' - 'smallint'/'tinyint'/'bigint' pks don't auto-assign
		//(inserting without them fails the not-null constraint). https://sqlite.org/lang_createtable.html#rowid
		//Every integral width already has integer affinity, so declaring them all 'integer' costs nothing and keeps
		//narrow sequences (access_resources.resource_id, access_providers.provider_id) auto-assigning.
		α ToString( EType type )Ι->string override{ using enum EType; return type==Int || type==UInt || type==Long || type==ULong || type==Int16 || type==UInt16 || type==Int8 || type==UInt8 ? "integer" : type==Blob ? "blob" : Syntax::ToString( type ); }
		α UniqueIndexNames()Ι->bool override{ return true; } //index names are schema-wide in sqlite.
		α UtcNow()Ι->sv override{ return "unixepoch()"; }
	};
}

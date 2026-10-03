#pragma once
#include <jde/fwk/str.h>
#include <jde/db/Value.h>
#include "../exports.h"

#define Φ ΓDB α
namespace Jde::DB{
	struct Column; struct Join; struct Table;
	enum class EOperator : uint8{Equal,NotEqual,Regex,Glob,In,NotIn,Greater,GreaterOrEqual,Less,LessOrEqual,ElementMatch};
	Φ ToOperator( sv op )ι->EOperator;
	Φ ToString( EOperator op )ι->string;

	//`glob:` is the one pattern language the QL filters speak - sqlite GLOB semantics, matched in-memory by QL::globMatch.
	//Only sqlite takes it verbatim, so the other dialects translate the *pattern* as well as swapping the keyword.
	//Case sensitivity does not survive the trip and cannot: GLOB is case-sensitive, T-SQL LIKE and MySQL REGEXP both
	//follow the column collation (usually case-insensitive).  A pattern may therefore match more rows in SQL than the
	//same pattern matches in the in-memory filter.
	Φ GlobToLike( sv glob )ι->string;  //T-SQL LIKE: '*'->'%', '?'->'_', a literal '%'/'_' bracket-escaped, '[!…]'->'[^…]'.
	Φ GlobToRegex( sv glob )ι->string; //anchored regex, for dialects with REGEXP but no GLOB.

	struct GlobClass{ bool Negate; sv Body; uint Close; };//Close indexes the ']'.
	Φ ParseGlobClass( sv glob, uint open )ι->optional<GlobClass>;

	Φ FindCommonType( sv name )ι->optional<EType>; //case-insensitive; nullopt when the name is not a common one.
	Φ CommonTypeName( EType type )ι->sv;          //empty for the types with no common spelling - the unsigned four, Guid, Blob - which ToString spells itself.

	struct ΓDB Syntax{
		Ω Instance()->const Syntax&;
		virtual ~Syntax()=default;
		α FormatOperator( const Column& col, EOperator op, uint size=1, SRCE )Ε->string;
		Ω IsPatternOperator( EOperator op )ι->bool{ return op==EOperator::Regex || op==EOperator::Glob; }
		β PatternOperator( EOperator op, SRCE )Ε->sv;              //SQL Server: LIKE, but no regex before 2025.
		β PatternParam( EOperator op, str pattern, SRCE )Ε->string;
		β AddDefault( sv tableName, sv columnName, Value dflt )Ι->string;
		β BoolLiteral( bool v )Ι->sv{ return v ? "1" : "0"; } //how a bit default is spelled in ddl.
		α DefaultLiteral( const Value& dflt )Ι->string;       //AddDefault's value: a bool through BoolLiteral, a string verbatim; anything else is CRITICAL and empty.
		β CanAddForeignKeys()Ι->bool{ return true; } //false (sqlite): no 'alter table add constraint' - fks only enforced when inline in create table.
		β CanSetDefaultSchema()Ι->bool{ return false; }
		β CatalogSelect()Ι->sv{ return "select db_name();"; }
		β CreatePrimaryKey( str tableName, str columns )Ι->string{ return Ƒ("CONSTRAINT {}_pk PRIMARY KEY( {} )", tableName, columns); } //columns: comma-separated for composite keys.
		β CreateProcSql()Ι->sv{ return "create or alter procedure"; }
		β DropProcSql( sv /*qualifiedName*/ )Ι->string{ return {}; } //empty: CreateProcSql already replaces.
		β DateTimeSelect( sv columnName )Ι->string{ return string{ columnName }; }
		β QuoteChars()Ι->std::pair<char,char>{ return {'[',']'}; }
		α EscapeDdl( sv sql )Ι->string;
		β GuidType()Ι->sv{ return "uniqueidentifier"; }
		β HasLength( EType type )Ι->bool;
		β HasCatalogs()Ι->bool{ return true; }
		β HasProcs()Ι->bool{ return true; }
		β HasSchemas()Ι->bool{ return true; }
		β HasUnsigned()Ι->bool{ return false; }
		β IdentityColumnSyntax()Ι->sv{ return "identity(1001,1)"; }
		β IdentityIsPrimaryKey()Ι->bool{ return false; } //true (sqlite): the identity syntax is the column's own primary key constraint, so a table keyed by its sequence alone gets no table-level one.
		β IdentitySelect()Ι->sv{ return "@@identity"; }
		β IndexName( sv /*tableName*/, sv indexName )Ι->string{ return string{indexName}; } //per-table index namespace; schema-wide dialects qualify with the table.
		β IsReservedWord( sv /*name*/ )Ι->bool{ return false; } //only words actually used as unprefixed object names - extend the dialect override when a new collision appears.
		β Limit( str syntax, uint limit, uint skip )Ε->string;
		α LimitOffset( str sql, uint limit, uint skip, sv unbounded )Ι->string; //the `limit n offset m` dialects' Limit; `unbounded` is their "every row" limit for a skip with no limit, since OFFSET needs a LIMIT.
		β NeedsIdentityInsert()Ι->bool{ return true; }
		β NowDefault()Ι->sv{ return UtcNow(); }
		β PrefixOut()Ι->bool{ return false; }
		β ProcParameterPrefix()Ι->sv{ return "@"; }
		β ProcStart()Ι->sv{ return "as\n\tset nocount on;\n"; }
		β ProcEnd()Ι->sv{ return {}; }
		β SchemaExistsSql()Ι->sv{ return "SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME = ?"; }
		β QualifiedName( sv schema, sv name )Ι->string{ return HasSchemas() ? Ƒ("{}.{}", schema, name) : string{name}; } //fully-qualified object name; schemaless dialects use the bare name.
		β SchemaSelect()Ι->sv{ return "select schema_name();"; } //empty (like CatalogSelect): SchemaName falls back to SysSchema without querying.
		β SpecifyIndexCluster()Ι->bool{ return true; }
		β SysSchema()Ι->sv{ return "dbo"; }
		β ToString( EType type )Ι->string;

		β UniqueIndexNames()Ι->bool{ return false; }
		//An insert suffix that turns a primary-key collision into an update of `updateColumns`, so a caller does not have
		//to branch on an UPDATE's row count to decide whether to insert.  That branch is not portable: MySQL reports rows
		//*changed* (a re-save of an identical value counts 0), sqlite and SQL Server report rows *matched*.
		//Empty means the dialect has no such form - SQL Server, where the caller's update-then-insert is correct anyway.
		β UpsertSuffix( const vector<sv>& /*keyColumns*/, const vector<sv>& /*updateColumns*/ )Ι->string{ return {}; }
		Ω SetList( const vector<sv>& columns, sv valuePrefix, sv valueSuffix )ι->string; //`a=<prefix>a<suffix>, b=…` - an upsert's update list, the value being the dialect's spelling of the incoming row's column.
		β UsingClause( const Join& join )Ι->string;
		β UtcNow()Ι->sv{ return "getutcdate()"; }
	};
	//The MySQL dialect lives with its driver, in libs/db/drivers/mysql/src/MySqlSyntax.h - like Sqlite::SqliteSyntax,
	//it is only reached through IDataSource::Syntax(), so Jde.DB never needs the type statically.
}
#undef Φ
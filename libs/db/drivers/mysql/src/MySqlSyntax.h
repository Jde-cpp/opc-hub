#pragma once
#include <jde/db/generators/Syntax.h>
#include <jde/db/generators/FromClause.h> //complete Join for UsingClause.
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>

namespace Jde::DB::MySql{
	//Driver-local dialect - resolved dynamically through IDataSource::Syntax(), so nothing in Jde.DB needs to know about it.
	//Header-only like Sqlite::SqliteSyntax: the tests include it directly, so no export from the MODULE is involved.
	struct MySqlSyntax final: Syntax{
		Ω Instance()->const MySqlSyntax&{ static const MySqlSyntax _instance; return _instance; }
		//SET DEFAULT takes a literal or a parenthesized expression - the bare CURRENT_TIMESTAMP a create statement accepts
		//is a syntax error here.  The catalog then reports the expression as `now()`, which MySqlDefault reads back as NowDefault.
		α SetDefault( sv tableName, sv columnName, const Value& dflt, sv )Ε->string override{
			const auto literal = dflt.is_string() && dflt.get_string()=="$now" ? Ƒ( "({})", NowDefault() ) : DefaultLiteral( dflt );
			return Ƒ( "ALTER TABLE {} ALTER COLUMN {} SET DEFAULT {}", tableName, columnName, literal );
		}
		α BoolLiteral( bool v )Ι->sv override{ return v ? "true" : "false"; }
		α DefaultConstraint( sv, sv )Ι->string override{ return {}; }
		α QuoteLiteral( sv text )Ι->string override{ return Syntax::QuoteLiteral( Str::Replace(text, "\\", "\\\\") ); } //a backslash escapes unless sql_mode has NO_BACKSLASH_ESCAPES, which the driver never sets.
		α CanSetDefaultSchema()Ι->bool override{ return true; }
		α CatalogSelect()Ι->sv override{ return {}; }
		α CreatePrimaryKey( str tableName, str columns )Ι->string override{ return Ƒ("CONSTRAINT {}_pk PRIMARY KEY( {} )", tableName, columns); } //columns: comma-separated for composite keys.
		α DateTimeSelect( sv columnName )Ι->string override{ return Ƒ( "UNIX_TIMESTAMP({})", columnName ); }
		α QuoteChars()Ι->std::pair<char,char> override{ return {'`','`'}; }
		α GuidType()Ι->sv override{ return "binary" ; }
		α ToString( EType type )Ι->string override{
			using enum EType;
			return type==Float ? "double" : type==SmallFloat ? "float" : type==Blob ? "blob" : Syntax::ToString( type );
		}
		α HasLength( EType type )Ι->bool override{
			using enum EType;
			return Syntax::HasLength( type ) || type==Bit || type==Guid;
		}
		α HasCatalogs()Ι->bool override{ return false; }
		α HasUnsigned()Ι->bool override{ return true; }
		α IdentityColumnSyntax()Ι->sv override{ return "AUTO_INCREMENT"; }
		α IdentitySelect()Ι->sv override{ return "LAST_INSERT_ID()"; }
		α IsReservedWord( sv name )Ι->bool override{ return name=="groups"; } //mysql 8+ window-function keyword; the only reserved word used as an unprefixed table name.
		α Limit( str sql, uint limit, uint skip )Ι->string override{ return LimitOffset( sql, limit, skip, "18446744073709551615" ); } //2^64-1 = "all rows" for a skip with no limit.
		α NeedsIdentityInsert()Ι->bool override{ return false; }
		α NowDefault()Ι->sv override{ return "CURRENT_TIMESTAMP"; }
		//The only dialect with a real regex engine (ICU, since 8.0), so `regex:` passes through.  There is no GLOB, and
		//LIKE would lose the character classes, so `glob:` is translated to an anchored REGEXP rather than to LIKE - that
		//mapping is total, where glob->LIKE is not.  Both are collation-cased, unlike the case-sensitive in-memory match.
		α PatternOperator( EOperator op, SRCE )Ε->sv override{
			if( op==EOperator::Regex || op==EOperator::Glob )
				return "regexp";
			throw Exception{ sl, Jde::ELogLevel::Debug, "Operator '{}' has no SQL form.", DB::ToString(op) };
		}
		α PatternParam( EOperator op, str pattern, SRCE )Ε->string override{
			PatternOperator( op, sl );
			return op==EOperator::Regex
				? pattern //ICU dialect: near enough to ECMAScript for the patterns the filters accept.
				: GlobToRegex( pattern );
		}
		α PrefixOut()Ι->bool override{ return true; }
		//`values(col)` rather than the 8.0.19 `as new` alias: MariaDB, which this driver's error categories also cover,
		//only understands the older form.
		α UpsertSuffix( const vector<sv>& /*keyColumns*/, const vector<sv>& updateColumns )Ι->string override{ return " on duplicate key update "+SetList( updateColumns, "values(", ")" ); }
		α CreateProcSql()Ι->sv override{ return "create procedure"; } //MySQL has no create-or-replace for procedures.
		α DropProcSql( sv qualifiedName )Ι->string override{ return Ƒ("drop procedure if exists {}", qualifiedName); }
		α ProcParameterPrefix()Ι->sv override{ return {}; }
		α ProcStart()Ι->sv override{ return "begin"; }
		α ProcEnd()Ι->sv override{ return "end"; }
		α SchemaSelect()Ι->sv override{ return "select database() from dual;"; }
		α SpecifyIndexCluster()Ι->bool override{ return false; }
		α SysSchema()Ι->sv override{ return "sys"; }
		//`using(col)` only when both sides name the same unaliased column; anything else falls back to the base `on a.x=b.y`.
		α UsingClause( const Join& join )Ι->string override{
			const auto& c1 = *join.To;
			return join.From->Name==c1.Name && join.ToAlias.empty() && join.FromAlias.empty()
				? Ƒ( "\n\t{}join {} using({})", join.Inner ? "" : "left ", c1.Table->SqlName(), c1.Name )
				: Syntax::UsingClause( join );
		}
		α UtcNow()Ι->sv override{ return "CURRENT_TIMESTAMP()"; }
	};
}
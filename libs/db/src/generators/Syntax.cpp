#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/db/generators/FromClause.h>
#define let const auto

namespace Jde{
	inline constexpr std::array<sv,11> OperatorStrings = { "=", "!=", "regex", "glob", "in", "nin", ">", ">=", "<", "<=", "elemMatch" };
	α DB::ToOperator( sv op )ι->EOperator{ return ToEnum<EOperator>( OperatorStrings, op ).value_or( EOperator::Equal ); }
	α DB::ToString( EOperator op )ι->string{ return FromEnum<EOperator>( OperatorStrings, op ); }
}
namespace Jde::DB{
	constexpr ELogTags _tags{ ELogTags::Sql };
	static Syntax _sqlInstance;

	constexpr std::array<std::pair<sv,EType>,22> CommonTypeNames{{ //one per EType that has a dialect-neutral name; see FindCommonType.
		{"datetime",EType::DateTime}, {"smalldatetime",EType::SmallDateTime}, {"float",EType::Float}, {"real",EType::SmallFloat},
		{"int",EType::Int}, {"bigint",EType::Long}, {"smallint",EType::Int16}, {"tinyint",EType::Int8},
		{"nvarchar",EType::VarWChar}, {"nchar",EType::WChar}, {"varchar",EType::VarChar}, {"char",EType::Char},
		{"ntext",EType::NText}, {"text",EType::Text}, {"uniqueidentifier",EType::Guid},
		{"varbinary",EType::VarBinary}, {"binary",EType::Binary}, {"image",EType::Image}, {"bit",EType::Bit},
		{"decimal",EType::Decimal}, {"numeric",EType::Numeric}, {"money",EType::Money}
	}};
	α FindCommonType( sv name )ι->optional<EType>{
		let iv = ToIV( name );
		auto p = find_if( CommonTypeNames, [&]( let& x ){ return ToIV(x.first)==iv; } );
		return p==CommonTypeNames.end() ? optional<EType>{} : p->second;
	}
	α CommonTypeName( EType type )ι->sv{
		auto p = find_if( CommonTypeNames, [&]( let& x ){ return x.second==type; } );
		return p==CommonTypeNames.end() ? sv{} : p->first;
	}
	α Syntax::Instance()->const Syntax&{ return _sqlInstance; }

	//OperatorStrings does double duty - it is the wire/display spelling for ToOperator/ToString *and* was indexed here for
	//SQL.  Only its six punctuation entries are valid SQL: `regex`/`glob`/`elemMatch` are names, and the old
	//`Ƒ("{}{}?", FQName, OperatorStrings[op])` glued them straight onto the column ("users.nameregex?"), which lexes as
	//one identifier.  The comparison operators are listed explicitly now, so a word operator added to the enum later
	//lands in the default and throws instead of silently emitting a broken clause.
	α Syntax::FormatOperator( const Column& col, EOperator op, uint size, SL sl )Ε->string{
		using enum EOperator;
		switch( op ){
		case Equal: case NotEqual: case Greater: case GreaterOrEqual: case Less: case LessOrEqual:
			//One value, one placeholder - the same count check the pattern operators below make, and for the same reason.
			//`eq:["a","b"]` used to render a single '?' against two bound params: N>=2 surfaced as an opaque driver bind
			//error (sqlite SQLITE_RANGE, MySQL wrong_num_params), and N==0 was worse - sqlite ran the statement with the
			//placeholder left NULL and quietly returned nothing.
			if( size!=1 )
				throw Exception{ sl, Jde::ELogLevel::Debug, "'{}' takes a single value, not {}.", DB::ToString(op), size };
			return Ƒ( "{}{}?", col.FQName(), OperatorStrings[(uint)op] ); //punctuation: self-delimiting, no spaces needed.
		case In: case NotIn:
			if( size==0 ) //`col in ()` is invalid SQL; an empty IN matches nothing, an empty NOT IN matches everything.
				return op==NotIn ? "1=1" : "1=0";
			else{
				string params;
				for( uint i=0; i<size; ++i )
					params += "?,";
				params.pop_back();
				return Ƒ( "{} {}({})", col.FQName(), op==NotIn ? "not in" : "in", params );
			}
		case Regex: case Glob:
			//A word operator needs its spaces.  One pattern, one placeholder: an array literal (`glob:["a*","b*"]`)
			//would otherwise render one '?' against N bound params and silently misalign the whole statement.
			if( size!=1 )
				throw Exception{ sl, Jde::ELogLevel::Debug, "'{}' takes a single pattern, not {}.", DB::ToString(op), size };
			return Ƒ( "{} {} ?", col.FQName(), PatternOperator(op, sl) );
		default:
			throw Exception{ sl, Jde::ELogLevel::Debug, "Operator '{}' has no SQL form.", DB::ToString(op) };
		}
	}

	α Syntax::PatternOperator( EOperator op, SL sl )Ε->sv{
		if( op==EOperator::Glob )
			return "like"; //T-SQL LIKE is glob's language with '%'/'_' for '*'/'?' - PatternParam does that rewrite.
		throw Exception{ sl, Jde::ELogLevel::Debug, "SQL Server has no '{}' operator (REGEXP_LIKE is 2025+); use 'glob'.", DB::ToString(op) };
	}
	α Syntax::PatternParam( EOperator op, str pattern, SL sl )Ε->string{
		PatternOperator( op, sl ); //C14: validates - throws this dialect's one unsupported-operator message.  Only Glob returns.
		return GlobToLike( pattern );
	}

	//'*'->'%' and '?'->'_'; a literal '%'/'_' becomes a one-element class ('[%]'), which is how T-SQL escapes them
	//without an ESCAPE clause - and which QL::globMatch already reads the same way, so the two stay in step.  Classes
	//pass through: T-SQL LIKE has them, spelled '[^…]' where glob also accepts '[!…]'.
	α ParseGlobClass( sv glob, uint open )ι->optional<GlobClass>{
		let negate = open+1<glob.size() && (glob[open+1]=='^' || glob[open+1]=='!');
		let bodyStart = open+1+negate;
		let close = glob.find( ']', bodyStart+1 );//+1: a ']' first in the body is a member, not the terminator.
		return close==sv::npos
			? optional<GlobClass>{}
			: GlobClass{ negate, glob.substr(bodyStart, close-bodyStart), (uint)close };
	}

	α GlobToLike( sv glob )ι->string{
		string y;
		for( uint i{}; i<glob.size(); ++i ){
			let c = glob[i];
			if( c=='*' )
				y += '%';
			else if( c=='?' )
				y += '_';
			else if( c=='%' || c=='_' )
				y += Ƒ( "[{}]", c );
			else if( c=='[' ){
				let parsed = ParseGlobClass( glob, i );
				if( !parsed ) //unterminated: a literal '[', as in sqlite - and '[' is a LIKE metacharacter too.
					y += "[[]";
				else{
					y += '[';
					if( parsed->Negate ) //glob accepts both spellings of negation; T-SQL only '^'.
						y += '^';
					y.append( parsed->Body );
					y += ']';
					i = parsed->Close;
				}
			}
			else
				y += c;
		}
		return y;
	}

	//'*'->'.*', '?'->'.', classes pass through, everything else escaped, and the whole thing anchored - glob matches the
	//entire value, where REGEXP is a search.
	α GlobToRegex( sv glob )ι->string{
		constexpr sv metacharacters{ ".^$*+?()[]{}|\\" };
		string y{ "^" };
		for( uint i{}; i<glob.size(); ++i ){
			let c = glob[i];
			if( c=='*' )
				y += ".*";
			else if( c=='?' )
				y += '.';
			else if( c=='[' ){
				let parsed = ParseGlobClass( glob, i );
				if( !parsed ) //unterminated: a literal '[', as in sqlite.
					y += "\\[";
				else{
					y += '[';
					if( parsed->Negate )
						y += '^';
					y.append( parsed->Body ); //ranges and members are spelled the same in both languages.
					y += ']';
					i = parsed->Close;
				}
			}
			else{
				if( metacharacters.find(c)!=sv::npos )
					y += '\\';
				y += c;
			}
		}
		return y+"$";
	}

	α Syntax::QuoteLiteral( sv text )Ι->string{
		return Ƒ( "'{}'", Str::Replace(text, "'", "''") );
	}
	α Syntax::DefaultLiteral( const Value& dflt )Ε->string{
		if( dflt.is_null() )
			return {};
		if( dflt.is_bool() )
			return string{ BoolLiteral(dflt.get_bool()) };
		if( dflt.is_string() )
			return dflt.get_string()=="$now" ? string{ NowDefault() } : QuoteLiteral( dflt.get_string() );
		if( dflt.is_double() )
			return Ƒ( "{}", dflt.get_double() );
		if( dflt.is_number() )
			return Ƒ( "{}", dflt.get_number<_int>() );
		if( dflt.Type()==EValue::Bytes && dflt.get_bytes().empty() ) //Column's placeholder for a nullable binary column (getDefault) - no default.
			return {};
		THROW( "A '{}' default has no ddl spelling.", dflt.TypeName() );
	}
	//T-SQL: a column's default is a constraint of its own, so a change is a drop and an add - in one transaction, or a
	//failed add leaves the column with no default.
	α Syntax::SetDefault( sv tableName, sv columnName, const Value& dflt, sv constraintName )Ε->string{
		auto add = Ƒ( "alter table {} add constraint {} default {} for {}", tableName, DefaultConstraint(tableName, columnName), DefaultLiteral(dflt), columnName );
		if( constraintName.empty() )
			return add;
		return Ƒ( "set xact_abort on; begin tran; alter table {} drop constraint {}; {}; commit", tableName, constraintName, add );
	}
	α Syntax::DefaultConstraint( sv tableName, sv columnName )Ι->string{
		return Ƒ( "DF_{}_{}", tableName.substr(tableName.rfind('.')+1), columnName ); //constraint names are schema-scoped - no schema in one.
	}

	α Syntax::EscapeDdl( sv sql )Ι->string{
		let [open, close] = QuoteChars();
		string y;
		for( let part : Str::Split(sql, '.') )
			y += open+string{part}+close+'.';
		y.pop_back();
		return y;
	}
	α Syntax::HasLength( EType type )Ι->bool{
		using enum EType;
		return type == VarChar || type == Binary || type == Char || type == VarBinary || type == VarWChar || type == WChar;
	}

	α Syntax::LimitOffset( str sql, uint limit, uint skip, sv unbounded )Ι->string{
		ASSERT( limit || skip );
		return skip
			? Ƒ( "{} limit {} offset {}", sql, limit ? std::to_string(limit) : string{unbounded}, skip )
			: Ƒ( "{} limit {}", sql, limit );
	}
	α Syntax::SetList( const vector<sv>& columns, sv valuePrefix, sv valueSuffix )ι->string{
		string y;
		for( let c : columns )
			y += Ƒ( "{}{}={}{}{}", y.empty() ? "" : ", ", c, valuePrefix, c, valueSuffix );
		return y;
	}
	α Syntax::Limit( str input, uint limit, uint skip )Ε->string{
		ASSERT( limit || skip );
		string sql = input+" offset "+std::to_string(skip)+" rows"; //T-SQL: OFFSET is mandatory before FETCH (emit it even for skip==0); both require an ORDER BY on the statement.
		if( limit )
			sql += " fetch next "+std::to_string(limit)+" rows only";
		return sql;
	}
	α joinType( bool inner )ι->string{
		return inner ? "" : "left ";
	}
	α Syntax::UsingClause( const Join& join )Ι->string{
		let& c0 = *join.From;
		let& c1 = *join.To;
		return Ƒ( "\n\t{0}join {3}{4} on {1}.{2}={5}.{6}",
			joinType(join.Inner),
			join.FromAlias.empty() ? c0.Table->SqlName() : join.FromAlias,
			c0.Name,
			c1.Table->SqlName(),
			join.ToAlias.empty() ? "" : " "+join.ToAlias,
			join.ToAlias.empty() ? c1.Table->SqlName() : join.ToAlias,
			c1.Name );
	}
	α Syntax::ToString( EType type )Ι->string{
		using enum EType;
		switch( type ){ //the spellings that depend on the dialect or have no common name; everything else is the table's.
		case UInt: return HasUnsigned() ? "int unsigned" : "int";
		case ULong: return HasUnsigned() ? "bigint(20) unsigned" : "bigint";
		case UInt16: return HasUnsigned() ? "smallint unsigned" : "smallint";
		case UInt8: return HasUnsigned() ? "tinyint unsigned" : "tinyint";
		case Guid: return string{ GuidType() };
		case Blob: return "varbinary(max)";
		default:
			if( let name = CommonTypeName(type); name.size() )
				return string{ name };
			ERR( "Unknown datatype({}).", (uint)type );
			return {};
		}
	}
}
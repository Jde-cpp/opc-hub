#include <jde/db/generators/WhereClause.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/db/generators/Functions.h>

#define let const auto

namespace Jde::DB{
	//Regex/Glob bind their pattern as a parameter, so swapping the keyword is only half the translation - the dialect has
	//to rewrite the value too (glob '*' is '%' to a LIKE, '.*' to a REGEXP).  Every other operator passes through.
	Ω patternParam( const Column& col, EOperator op, Value param, SL sl )ε->Value{
		if( !Syntax::IsPatternOperator(op) ) //C14: one membership test, shared with FormatOperator.
			return param;
		if( !param.is_string() )
			throw Exception{ sl, Jde::ELogLevel::Debug, "'{}' needs a string pattern, not a '{}'.", DB::ToString(op), param.TypeName() };
		return Value{ col.Table->Syntax().PatternParam(op, param.get_string(), sl) };
	}

	//Object-to-Object comparison.  Restricted to the six punctuation operators, because this composes both sides itself
	//and so cannot go through Syntax::FormatOperator/PatternParam: `regex`/`glob`/`nin`/`elemMatch` are ToString's *wire*
	//spellings, not SQL, and the pattern operators additionally need their bound value rewritten per dialect.  Emitting
	//them here produced `t.col nin (?,?)` and untranslated globs - the same wire-string-into-SQL bug 0e59d753 fixed in
	//FormatOperator, which this path was not given.  Use WhereClause::Add( col, op, … ) for those.
	WhereClause::WhereClause( const Object& a, EOperator op, const Object& b, SL sl )ε{
		using enum EOperator;
		switch( op ){
		case Equal: case NotEqual: case Greater: case GreaterOrEqual: case Less: case LessOrEqual: break;
		default:
			throw Exception{ sl, Jde::ELogLevel::Debug, "Operator '{}' has no SQL form here - it needs Syntax::FormatOperator, which takes a Column.", DB::ToString(op) };
		}
		auto clause = DB::ToString( a );
		auto aParams = GetParams( a );//`a` binds either way: a Coalesce renders '?' for its Value members, and the null
		move( aParams.begin(), aParams.end(), back_inserter(_params) );//branch below used to leave those unbound.
		if( b.index()==underlying(EObject::Value) && get<Value>(b).is_null() ){
			if( op==Equal )
				clause = Ƒ( "{} is null", move(clause) );
			else if( op==NotEqual )
				clause = Ƒ( "{} is not null", move(clause) );
			else
				throw Exception{ sl, Jde::ELogLevel::Debug, "Null value not allowed for operator '{}'.", DB::ToString(op) };
		}
		else{
			clause = Ƒ( "{} {} {}", move(clause), DB::ToString(op), DB::ToString(b) );
			auto bParams = GetParams( b );
			move( bParams.begin(), bParams.end(), back_inserter(_params) );
		}
		_clauses.push_back( move(clause) );
	}
	WhereClause::WhereClause( AliasCol&& c, Value::Underlying param, SL sl )ε:
		WhereClause{ move(c), EOperator::Equal, Value{move(param)}, sl }
	{}

	WhereClause::WhereClause( vector<WhereClause>&& clauses )ε{
		_clauses.reserve( clauses.size() );
		for( auto&& subTable : clauses ){
			move( begin(subTable._clauses), end(subTable._clauses), back_inserter(_clauses) );
			move( begin(subTable._params), end(subTable._params), back_inserter(_params) );
		}
	}

	α WhereClause::operator+=( const WhereClause& subTable )ι->WhereClause&{
		_clauses.insert( end(_clauses), begin(subTable._clauses), end(subTable._clauses) );
		_params.insert( end(_params), begin(subTable._params), end(subTable._params) );
		return *this;
	}
	α WhereClause::Add( sp<Column> col, EOperator op, Value param, SL sl )ε->void{
		if( param.is_null() ){
			string prefix;
			if( op==EOperator::NotEqual )
				prefix = "not ";
			else if( op!=EOperator::Equal )
				throw Exception{ sl, Jde::ELogLevel::Debug, "Null value not allowed for operator '{}'.", DB::ToString(op) };
			_clauses.push_back( Ƒ("{} is {}null", col->FQName(), prefix) );
		}else{
			auto translated = patternParam( *col, op, move(param), sl );
			_clauses.push_back( col->Table->Syntax().FormatOperator(*col, op, 1, sl) );
			_params.push_back( move(translated) );
		}
	}

	//Both overloads below run everything that can throw before committing anything, so a rejected Add leaves the clause
	//exactly as it found it.  Two things throw here: FormatOperator, for a Regex with a count other than 1 or a Glob with
	//none (and for Regex on sqlite at all), and patternParam, for a non-string pattern - the latter part way through the list.
	α WhereClause::Add( sp<Column> col, EOperator op, vector<Value> inParams, SL sl )ε->void{
		auto clause = col->Table->Syntax().FormatOperator( *col, op, inParams.size(), sl );
		vector<Value> translated; translated.reserve( inParams.size() );
		for( auto& param : inParams )
			translated.emplace_back( patternParam(*col, op, move(param), sl) );
		move( translated.begin(), translated.end(), back_inserter(_params) );
		_clauses.push_back( move(clause) );
	}

	//A null in the list is the column's null:  `x in(…) or x is null`, and for a negative operator `x not in(…) and x is not
	//null`.  #590: `not in` and `!=` are unknown for a null, so without one in the list a nullable column's null rows went with
	//the listed values;  they are kept, and a caller that wants them out says so - `nin:[…,null]`.
	α WhereClause::Add( sp<Column> col, EOperator op, vector<Value> inParams, bool haveNull, SL sl )ε->void{
		let negative = op==EOperator::NotIn || op==EOperator::NotEqual;
		if( !haveNull && !(negative && col->IsNullable && !inParams.empty()) )
			return Add( col, op, move(inParams), sl );
		string clause;
		vector<Value> translated; translated.reserve( inParams.size() );
		if( inParams.empty() )
			clause = Ƒ( "{} is {}null", col->FQName(), negative ? "not " : "" );
		else{
			auto inClause = col->Table->Syntax().FormatOperator( *col, op, inParams.size(), sl );//`regex:[null,"a","b"]` throws here, with 2 params still un-committed.
			for( auto& param : inParams )
				translated.emplace_back( patternParam(*col, op, move(param), sl) );
			clause = haveNull && negative
				? Ƒ( "({} and {} is not null)", move(inClause), col->FQName() )
				: Ƒ( "({} or {} is null)", move(inClause), col->FQName() );
		}
		move( translated.begin(), translated.end(), back_inserter(_params) );
		_clauses.push_back( move(clause) );
	}

	α WhereClause::Add( const DB::Criteria& criteria )ε->void{
		Add( criteria.Column, EOperator::Equal, criteria.Value );
	}

	α WhereClause::Move()ι->string{ //A12: the old body's `Str::Join( move(_clauses) )` bound to Join's const& and never moved - this is what it did, plus the consumption its name promised.
		auto y = ToString();
		_clauses.clear();
		return y;
	}
	α WhereClause::ToString()Ι->string{
		return _clauses.size() ? "where "+Str::Join( _clauses, " and " ) : string{};
	}
}
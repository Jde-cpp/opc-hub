#include <jde/ql/types/FilterQL.h>
#include <boost/regex.hpp>
#include <jde/fwk/chrono.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Syntax.h>//C9: ParseGlobClass - named directly here, so included directly.
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/types/TableQL.h>
#include "../qlInternal.h"

#define let const auto

//The state cap arrives as a -D from libs/ql/src/CMakeLists.txt, not a #define here: the PUBLIC precompiled header is
//force-included ahead of this file's first line, so a #define could land after boost/regex/config.hpp had already baked
//in its 100,000,000 default.  Assert it, because a lost -D would widen the bound back silently - measurably so only for
//the shallow-but-wide patterns; boost's recursion guard covers the nested ones at any cap.
static_assert( BOOST_REGEX_MAX_STATE_COUNT==100'000, "Jde.QL must lower boost's regex state cap - see libs/ql/src/CMakeLists.txt." );

namespace Jde::QL{
	constexpr ELogTags _tags{ ELogTags::QL };
	inline constexpr std::array<sv,11> QLOperatorStrings = { "eq", "ne", "regex", "glob", "in", "nin", "gt", "gte", "lt", "lte", "elemMatch" };//matches GraphQL

	//Whether c matches the class starting at pattern[p] ('['), advancing p past the ']'.  nullopt when the class is
	//unterminated - the '[' is a literal then, as in sqlite.
	//C9: the extent and the negation come from DB::ParseGlobClass, the one statement of that grammar, shared with
	//GlobToLike and GlobToRegex.  Only the *matching* is ql's - the other two translate the body rather than test it.
	Ω matchClass( sv pattern, uint& p, char c )ι->optional<bool>{
		let parsed = DB::ParseGlobClass( pattern, p );
		if( !parsed )
			return {};
		let& body = parsed->Body;
		bool matched{};
		for( uint i{}; i<body.size(); ){
			if( i+2<body.size() && body[i+1]=='-' ){//a trailing '-' has nothing to span, so it is a member - and Body excludes the ']' that used to have to be checked for here.
				matched = matched || (c>=body[i] && c<=body[i+2]);
				i += 3;
			}
			else{
				matched = matched || c==body[i];
				++i;
			}
		}
		p = parsed->Close+1;
		return matched!=parsed->Negate;
	}

	//sqlite GLOB semantics: '*' any run, '?' one character, '[abc]'/'[a-z]'/'[^abc]' classes.  Iterative with a single
	//backtrack point, so the cost is bounded by text*pattern - a regex would let a hostile pattern backtrack exponentially,
	//and `glob:"*abc*"` isn't even a legal one (nothing for the leading '*' to repeat), which is what this used to be.
	Ω globMatch( sv text, sv pattern )ι->bool{
		uint t{}, p{}, starT{};
		optional<uint> starP;
		while( t<text.size() ){
			uint nextP{ p };
			bool matched{};
			if( p<pattern.size() ){
				if( pattern[p]=='*' ){
					starP = p++;
					starT = t;
					continue;
				}
				if( pattern[p]=='[' ){
					if( let inClass = matchClass(pattern, nextP, text[t]); inClass )
						matched = *inClass;
					else{
						matched = text[t]=='[';
						nextP = p+1;
					}
				}
				else{
					matched = pattern[p]=='?' || pattern[p]==text[t];
					nextP = p+1;
				}
			}
			if( matched ){
				p = nextP;
				++t;
			}
			else if( starP ){ //let the last '*' swallow one more character.
				p = *starP+1;
				t = ++starT;
			}
			else
				return false;
		}
		while( p<pattern.size() && pattern[p]=='*' )
			++p;
		return p==pattern.size();
	}

	//Built once per FilterValue: glob keeps the pattern text, regex keeps the compiled boost::regex.  Every row used to
	//construct a std::regex from the json - and SubscribeLog::Write tests every log entry against every subscriber's
	//filter, so that was a compile per entry per subscription.
	struct Pattern final{
		Pattern( string text, up<const boost::regex>&& regex )ι: Text{ move(text) }, Regex{ std::move(regex) }{} //qualified: ADL on up<boost::regex> also finds boost::move.
		α Match( str value )Ι->bool;
		string Text;
		up<const boost::regex> Regex;
	private:
		mutable atomic<bool> _tooComplex{}; //shared as sp<const Pattern> across rows and threads - see Match.
	};

	//boost, not std: the standard puts no step limit on std::regex_match, so on libc++ a *valid* pattern like `(a+)+$`
	//backtracks without bound - once per row, on the fan-out thread.  (The MSVC STL happens to raise error_complexity
	//of its own accord, which is why this only ever threatened the linux build.)  boost guarantees the bound on both:
	//a recursion-depth guard stops the nested-quantifier shapes, and BOOST_REGEX_MAX_STATE_COUNT - lowered in
	//libs/ql/src/CMakeLists.txt - the shallow-but-wide ones.  Either way the abort arrives as an exception, in ~1ms.
	//The bound is per match call, so the first abort poisons the whole pattern: one filter is tested against every log
	//entry, and a 10k-entry burst would otherwise pay the full budget 10k times over.
	//Deliberately silent.  Match runs under SubscribeLog::Write's shared_lock and every log macro re-enters that same
	//logger, so a WARN here would take a recursive shared_lock on a shared_mutex - UB, and a deadlock against any queued
	//writer.  A poisoned pattern reads as "matches nothing", the same as one that failed to compile - and that case *is*
	//warned about, in makePattern, which runs off this path.
	α Pattern::Match( str value )Ι->bool{
		if( !Regex )
			return globMatch( value, Text );
		if( _tooComplex.load(std::memory_order_relaxed) )
			return false;
		try{
			return boost::regex_match( value, *Regex );
		}
		catch( const std::exception& ){ //boost::regex_error derives from runtime_error; error_complexity/error_stack land here.
			_tooComplex.store( true, std::memory_order_relaxed );
		}
		return false;
	}

	//A pattern that can't be used (wrong json type, too long, doesn't compile) yields null, i.e. the filter matches nothing -
	//as before, except it is now said out loud once instead of throwing per row into Test's catch.
	Ω makePattern( DB::EOperator op, const jvalue& value )ι->sp<const Pattern>{
		let spelling = QL::ToString( op ); //qualified: DB::ToString(EOperator) is an equally good ADL match.
		let text = value.try_as_string();
		if( !text ){
			WARN( "A '{}' filter needs a string pattern, not '{}' - it can not match.", spelling, serialize(value) );
			return {};
		}
		if( let maxLength = op==DB::EOperator::Glob ? MaxPatternLength : MaxRegexLength; text->size()>maxLength ){
			WARN( "A '{}' pattern of {} characters exceeds the {} limit - it can not match.", spelling, text->size(), maxLength );
			return {};
		}
		string pattern{ *text };
		if( op==DB::EOperator::Glob )
			return ms<Pattern>( move(pattern), nullptr );
		try{
			return ms<Pattern>( string{}, mu<const boost::regex>(pattern) );
		}
		catch( const std::exception& e ){
			WARN( "regex '{}' does not compile ({}) - it can not match.", pattern, e.what() );
		}
		return {};
	}

	//The filter's own literal(s) as TimePoints, parsed once here rather than per row.  Empty unless *every* literal parses,
	//which is also how Test decides whether a time column can be compared chronologically at all - an unparseable literal
	//keeps the old string behaviour rather than silently matching nothing.
	Ω makeTimes( const jvalue& value )ι->vector<TimePoint>{
		vector<TimePoint> y;
		auto add = [&y]( const jvalue& v )ι->bool {
			let text = v.try_as_string();
			//shape test first: Chrono::ToTimePoint wants %FT%T and throws otherwise, and most filter literals ("text",
			//"message", ...) are not times at all - this runs per filter, but there is no reason to throw per filter.
			if( !text || text->size()<19 || (*text)[10]!='T' )
				return false;
			try{
				y.push_back( Chrono::ToTimePoint(string{*text}) );
				return true;
			}
			catch( const runtime_error& ){
			}
			return false;
		};
		if( let a = value.try_as_array(); a ){
			for( let& v : *a ){
				if( !add(v) )
					return {};
			}
			return y;
		}
		return add( value ) ? y : vector<TimePoint>{};
	}

	FilterValue::FilterValue( DB::EOperator op, jvalue value )ι:
		Operator{ op },
		Value{ move(value) },
		_pattern{ op==DB::EOperator::Regex || op==DB::EOperator::Glob ? makePattern(op, Value) : nullptr },
		//keyed off the operator, not off _pattern: a regex/glob whose pattern failed to compile is still a text match.
		_times{ op==DB::EOperator::Regex || op==DB::EOperator::Glob ? vector<TimePoint>{} : makeTimes(Value) }
	{}

	α Filter::Test( const DB::Value::Underlying& value, const vector<FilterValue>& filters, ELogTags logTags )ι->bool{
		bool passesFilters{true};
		for( auto p=filters.begin(); passesFilters && p!=filters.end(); ++p )
			passesFilters = p->Test( value, logTags );
		return passesFilters;
	}
	α Filter::TestOr( str columnName, uint value )Ι->bool{
		bool valid{ true };
		if( let filters = ColumnFilters.find(columnName); filters!=ColumnFilters.end() ){
			for( let& filterValue : filters->second ){
				if( valid = filterValue.TestAnd(value); valid )
					break;
			}
		}
		return valid;
	}
	α Filter::ToString( str colName )Ι->string{
		auto p = ColumnFilters.find( colName );
		return p==ColumnFilters.end() || p->second.empty() ? "none" : p->second[0].ToString();
	}

	//A time column compares as a TimePoint, never through its rendered string.  ToIsoString is variable width - a whole
	//second drops the fraction - so "2026-08-02T10:00:00.250000Z" is lexicographically *less* than "2026-08-02T10:00:00Z"
	//('.'=0x2E < 'Z'=0x5A at index 19).  gt on a whole-second boundary therefore dropped every sub-second entry inside that
	//second, and lt on a sub-second bound dropped the whole-second entry below it - each one classified backwards.
	α FilterValue::TestTime( TimePoint value )Ι->bool{
		using enum DB::EOperator;
		let one = _times.size()==1;//a scalar operator against an array literal is nonsense; keep it matching nothing.
		switch( Operator ){
		case Equal: return one && value==_times[0];
		case NotEqual: return !one || value!=_times[0];
		case Greater: return one && value>_times[0];
		case GreaterOrEqual: return one && value>=_times[0];
		case Less: return one && value<_times[0];
		case LessOrEqual: return one && value<=_times[0];
		case In: return find( _times, value )!=_times.end();
		case NotIn: return find( _times, value )==_times.end();
		default: return false;//Regex/Glob never reach here (no _times); ElementMatch matches nothing, as in the switch below.
		}
	}

	α FilterValue::Test( const DB::Value& db, ELogTags logTags )Ι->bool{
		using namespace Json;
		bool passesFilters{};
		try{
			if( db.Type()==DB::EValue::Time && _times.size() )
				return TestTime( db.get_time() );
			switch( Operator ){
			using enum DB::EOperator;
			case Equal: passesFilters = Value==db.ToJson(); break;
			case NotEqual: passesFilters = Value!=db.ToJson(); break;
			case Regex: case Glob: passesFilters = _pattern && _pattern->Match( db.ToString() ); break; //compiled/validated in the ctor; a null pattern matches nothing.
			case In: passesFilters = Json::Find( Value, db.ToJson() ); break;
			case NotIn: passesFilters = !Json::Find( Value, db.ToJson() ); break;
			case Greater: passesFilters = db.ToJson() > Value; break;
			case GreaterOrEqual: passesFilters = db.ToJson() >= Value; break;
			case Less: passesFilters = db.ToJson() < Value; break;
			case LessOrEqual: passesFilters = db.ToJson() <= Value; break;
			case ElementMatch: BREAK; break;
			}
		}
		catch( Exception& e ){
			e.SetTags( logTags | ELogTags::Exception );
			e.PrependWhat("FilterValue::Test exception: ");
		}
		catch( const runtime_error& e ){
			DBGT( logTags | ELogTags::Exception, "FilterValue::Test runtime_error={}", e.what() );
		}
		catch( ... ){
			CRITICALT( logTags | ELogTags::Exception, "FilterValue::unknown exception" );
		}
		return passesFilters;
	}
	α FilterValue::TestAnd( uint value )Ι->bool{
		auto filter = Value.try_to_number<uint>();
		return filter && (*filter & value);
	}

	α FilterValue::ToString()Ι->string{
		return Ƒ("{}={}", DB::ToString(Operator), serialize(Value));
	}
}
namespace Jde{
	α QL::ToString( DB::EOperator op )ι->string{
		return FromEnum( QLOperatorStrings, op );
	}

	α QL::ToQLOperator( string op )ι->DB::EOperator{
		return ToEnum<DB::EOperator>( QLOperatorStrings, op ).value_or( DB::EOperator::Equal );
	}

	α QL::FilterColumn( const DB::Table& dbTable, sv jsonName, SL sl )ε->sp<DB::Column>{
		if( jsonName=="id" )
			return dbTable.Extends ? dbTable.Extends->GetPK() : dbTable.GetPK();//can't have left join users where users.id=42
		let name = DB::Names::FromJson( jsonName );
		if( auto column = dbTable.FindColumn(name); column )
			return column;
		//#20: `provider` is not a column of users_ql - it is how provider_id *renders*, through the enum table's name.
		//Filtering and ordering resolve to the id column, so neither depends on the display join being in the statement.
		if( auto pEnum = dbTable.FindColumn(string{name}+"_id"); pEnum && pEnum->PKTable )
			return pEnum;
		return dbTable.GetColumnPtr( name, sl );//throws, naming table and column, exactly as before.
	}
	//An enum column filtered by its display name ("Google"), which is the only spelling the client has - DB::Value{UInt,string}
	//would throw.  The map is the one addColumn renders through.
	Ω toFilterValue( const DB::Column& column, const jvalue& v, SL sl )ε->DB::Value{
		//The pk table's own view is where the names live (providers -> providers_ql), which is how addColumn renders them - and
		//it is the rule to follow rather than Column::IsEnum(), which is false for providers (its view has more than two columns).
		if( let s = v.if_string(); s && column.PKTable ){
			let pkTable = column.PKTable->QLView ? column.PKTable->QLView : column.PKTable;
			if( pkTable->FindColumn("name") ){
				for( let& [id,name] : QL::GetEnumValues(*pkTable, sl) ){
					if( name==*s )
						return DB::Value{ column.Type, id };
				}
				THROWSL( "[{}.{}]'{}' is not a value of '{}'.", column.Table ? column.Table->Name : string{}, column.Name, string{*s}, pkTable->Name );
			}
		}
		return DB::Value{ column.Type, v };
	}

	α QL::ToWhereClause( const TableQL& table, const DB::Table& dbTable, bool includeDeleted )ε->DB::WhereClause{
		table.CheckVariables();
		DB::WhereClause where;
		for( let& [name,filters] : table.Filter().ColumnFilters ){
			if( name=="orderBy" || name=="limit" || name=="offset" || name=="skip" )
				continue;
			auto column = FilterColumn( dbTable, name );
			if( name=="deleted" )//an explicit filter on deleted is the whole predicate.  It set this false, which ANDed the implicit `deleted is null` onto it:  "not null" and a date could never match, only the null the implicit one already meant (reviews/m3-closing.md #16).
				includeDeleted = true;
			for( auto& filter : filters ){
				auto& value = filter.Value;
				const jvalue* json{};
				if( let array = value.try_as_array(); array ){
					vector<DB::Value> params;
					bool haveNull{};
					for( let& v : *array ){
						if( v.is_null() )
							haveNull = true;
						else
							params.push_back( toFilterValue(*column, v, SRCE_CUR) );
					}
					where.Add( column, filter.Operator, move(params), haveNull );
				}
				else{
					if( value.is_string() || value.is_number() || value.is_null() || value.is_bool() )
						json = &value;
					else if( let o = value.try_as_object(); o && o->size() ){
						let& first = *o->begin();
						json = &first.value();
					}
					else
						THROW("Invalid filter value type '{}'.", Json::Kind(value.kind()) );
					if( json->is_array() )
						where.Add( column, filter.Operator, DB::ToValue<uint>(Json::FromArray<uint>(json->get_array())) );
					else
						where.Add( column, filter.Operator, toFilterValue(*column, *json, SRCE_CUR) );
				}
			}
		}
		if( auto deleted = includeDeleted ? nullptr : dbTable.FindColumn("deleted"); deleted )
			where.Add( deleted, DB::Value{} );
		return where;
	}
}
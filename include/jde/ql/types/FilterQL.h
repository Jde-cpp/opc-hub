#pragma once
#include <jde/db/generators/WhereClause.h>

namespace Jde::DB{ struct Value; struct Table; }

namespace Jde::QL{
	struct TableQL; struct Pattern;
	α ToString( DB::EOperator op )ι->string;
	α ToQLOperator( string op )ι->DB::EOperator;
	inline constexpr uint MaxPatternLength{ 1024 };
	inline constexpr uint MaxRegexLength{ 64 };

	struct FilterValue final{
		FilterValue( DB::EOperator op, jvalue value )ι;
		DB::EOperator Operator;
		jvalue Value;
		α Test( const DB::Value& value, ELogTags logTags )Ι->bool;
		Ŧ Test( T value )Ι->bool;
		α TestAnd( uint value )Ι->bool;
		α ToString()Ι->string;
	private:
		α TestTime( TimePoint value )Ι->bool;
		sp<const Pattern> _pattern; //regex/glob only, and null when the pattern was unusable - built once here, never per row.  Defined in FilterQL.cpp so <boost/regex.hpp> - and the state cap it is compiled with - stay out of this header.
		vector<TimePoint> _times; //Value parsed as time(s), once, so a time column compares chronologically - see TestTime.  Empty unless every literal parsed.
	};
	struct Filter final{
		α Empty()Ι->bool{ return ColumnFilters.empty() /*&& !StartTime && !EndTime*/; }
		Ŧ Test( str columnName, const T& value )Ι->bool;
		α TestOr( str columnName, uint value )Ι->bool;
		Ŧ TestF( str columnName, function<T()> f )Ι->bool;
		Ω Test( const DB::Value::Underlying& value, const vector<FilterValue>& filters, ELogTags logTags )ι->bool;
		α ToString( str colName )Ι->string;
		flat_map<string,vector<FilterValue>> ColumnFilters;
	private:
		template<class F> α TestWith( str columnName, const F& value )Ι->bool;
	};
	α ToWhereClause( const TableQL& table, const DB::Table& schemaTable )ε->DB::WhereClause;
	//The column a filter or an order-by names, resolved the way addColumn resolves a *selected* column - the pk for "id", the
	//column itself, or, for an enum's display name, the <name>_id it renders through (#20).  Throws naming the table if none.
	α FilterColumn( const DB::Table& dbTable, sv jsonName, SRCE )ε->sp<DB::Column>;

	Ŧ FilterValue::Test( T value )Ι->bool{
		return Test( DB::Value{move(value)}, ELogTags::QL );
	}
	//`value()` runs only when the column has a filter - what TestF is for; Test hands over the value it already holds.
	template<class F> α Filter::TestWith( str columnName, const F& value )Ι->bool{
		if( auto it = ColumnFilters.find(columnName); it!=ColumnFilters.end() ){
			decltype(auto) v = value();
			for( const auto& filterValue : it->second ){
				if( !filterValue.Test(v) )
					return false;
			}
		}
		return true;
	}
	Ŧ Filter::Test( str columnName, const T& value )Ι->bool{
		return TestWith( columnName, [&]()->const T&{ return value; } );
	}
	Ŧ Filter::TestF( str columnName, function<T()> f )Ι->bool{
		return TestWith( columnName, f );
	}
}
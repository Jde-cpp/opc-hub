#include <jde/db/generators/Object.h>
#include <jde/fwk/str.h>
#include <jde/db/generators/Functions.h>

#pragma GCC diagnostic ignored "-Wswitch"

namespace Jde {
	α DB::GetParams( const Object& o )ι->vector<Value>{
		vector<DB::Value> params;
		switch( (EObject)o.index() ){
			using enum EObject;
			case Value: params.push_back(get<DB::Value>(o)); break;
			case Values: params = get<vector<DB::Value>>(o); break;
			case Coalesce: params = get<DB::Coalesce>(o).Params(); break;
		}
		return params;
	}

	α DB::operator==(const Object& a, const Object& b)ι->bool{
		if( a.index()!=b.index() )
			return false;
		switch( (EObject)a.index() ){
			using enum EObject;
			case Value: return get<DB::Value>(a)==get<DB::Value>(b);
			case Values: return get<vector<DB::Value>>(a)==get<vector<DB::Value>>(b);
			case AliasColumn: return get<AliasCol>(a)==get<AliasCol>(b);
			case Coalesce: return get<DB::Coalesce>(a).ToString()==get<DB::Coalesce>(b).ToString();
			case Count: return get<DB::Count>(a).ToString()==get<DB::Count>(b).ToString();
		}
		return false;
	}

	α DB::ToString( const Object& o )ι->string{
		switch( (EObject)o.index() ){
			using enum EObject;
			case AliasColumn: return get<AliasCol>(o).ToString();
			case Value: return "?";
			case Coalesce: return get<DB::Coalesce>(o).ToString();
			case Count: return get<DB::Count>(o).ToString();
			case Values:{
				const auto& values = get<vector<DB::Value>>(o);
				string s( "(" ); s.reserve( values.size()*2 + 1 );
				for( uint i=0; i<values.size(); ++i )
					s += i ? ",?" : "?";
				s += ')';
				return s;
			}
		}
		return {};
	}
}
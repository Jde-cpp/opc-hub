#pragma once
#include <jde/db/Key.h>
#include <jde/ql/types/FilterQL.h>

#define let const auto
namespace Jde::QL{
	struct Input{
		static constexpr sv Escape{ "\b$" };
		Input( jobject&& args, sp<jobject> variables )ι: Args{ move(args) }, Variables{ move(variables) }{}
		α ArgString()Ι->string;
		Ŧ Find( sv name )Ι->optional<T>;
		template<class T=jvalue> α FindPtr( sv name )Ι->const T*;
		template<class T=jvalue> α As( sv name, SRCE )Ε->const T&;
		template<class T> α AsPathNumber( sv path, SRCE )Ε->T;
		template<class T=jvalue> α AsPathRef( sv path, SRCE )Ε->const T&;
		Ŧ AsNumber( sv name, SRCE )Ε->T;
		Ŧ TryNumber( sv name )Ι->optional<T>;
		template<class T=uint> α Id()Ι->T;
		template<class T=uint> α FindId()Ι->optional<T>;
		α FindKey()Ι->optional<DB::Key>;
		α GetKey( SRCE )Ε->DB::Key;
		α ExtrapolateVariables()Ι->jobject;
		α UnboundVariables()Ι->vector<string>;
		α CheckVariables( SRCE )Ε->void;
		α Filter()Ι->const Filter&;
		β JTableName()Ι->string=0;
		α Limit()Ι->uint{ return TryNumber<uint>( "limit" ).value_or(0); }
		α Offset()Ι->uint{ auto offset = TryNumber<uint>( "offset" ); return offset ? *offset : TryNumber<uint>( "skip" ).value_or(0); }
		α OrderByJson()Ι->const vector<std::pair<string,bool>>&;

		jobject Args;
		sp<jobject> Variables;
		mutable optional<QL::Filter> _filter;
	private:
		α VariableName( const jvalue& v )Ι->string;
		α VariablesString()Ι->string{ return Variables ? serialize(*Variables) : "{}"; }
		mutable optional<vector<std::pair<string,bool>>> _orderBy;
	};

	Ξ Input::VariableName( const jvalue& v )Ι->string{
		if( !v.is_string() )
			return {};
		if( let s = v.get_string(); s.starts_with(Escape) )
			return string{sv{s}.substr(2)};
		return {};
	}

	template<> Ξ Input::AsPathRef<jvalue>( sv path, SL sl )Ε->const jvalue&{
		auto arg = Json::FindValue( Args,  path );
		THROW_IFSL( !arg, "Could not find path '{}' in query: {}", path, serialize(Args) );
		if( let variableName = VariableName(*arg); variableName.size() ){
			auto varValue = Variables->if_contains( variableName );
			THROW_IFSL( !varValue, "Could not find variable '{}' in variables: {}", variableName, VariablesString() );
			return *varValue;
		}
		return *arg;
	}

	Ŧ Input::AsPathNumber( sv path, SL sl )Ε->T{
		auto& v = Input::AsPathRef( path, sl );
		auto y = v.try_to_number<T>();
		THROW_IFSL( !y, "Could not convert path '{}:{}' to number ", path, serialize(v) );
		return *y;
	}

	template<> Ξ Input::FindPtr<jvalue>( sv name )Ι->const jvalue*{
		auto value = Args.if_contains( name );
		if( auto variableName = value!=nullptr ? VariableName( *value ) : string{}; variableName.size() )
			value = Variables->if_contains( variableName );
		return value;
	}
	template<> Ξ Input::FindPtr( sv name )Ι->const jstring*{
		auto value = FindPtr<jvalue>( name );
		return value && value->is_string() ? &value->get_string() : nullptr;
	}
	template<> Ξ Input::FindPtr( sv name )Ι->const jobject*{
		auto value = FindPtr<jvalue>( name );
		return value && value->is_object() ? &value->get_object() : nullptr;
	}
	template<> Ξ Input::FindPtr( sv name )Ι->const jarray*{
		auto value = FindPtr<jvalue>( name );
		return value && value->is_array() ? &value->get_array() : nullptr;
	}

	template<> Ξ Input::Find( sv key )Ι->optional<bool>{
		auto p = FindPtr<jvalue>( key );
		return p && p->is_bool() ? p->get_bool() : optional<bool>{};
	}

	Ŧ Input::As( sv key, SL sl )Ε->const T&{
		auto p = FindPtr<T>( key );
		THROW_IFSL( !p, "Could not find key '{}' in query: {}, variables: {}", key, serialize(Args), serialize(*Variables) );
		return *p;
	}

	Ŧ Input::TryNumber( sv name )Ι->optional<T>{
		optional<T> y;
		if( auto p = FindPtr(name); p && p->is_number() ){
			if( auto n = p->try_to_number<T>(); n )
				y = *n;
		}
		return y;
	}
	Ŧ Input::AsNumber( sv name, SL sl )Ε->T{
		auto y = TryNumber<T>( name );
		THROW_IFSL( !y, "Could not find key '{}' in mutation query: {}, variables: {}", name, serialize(Args), serialize(*Variables) );
		return *y;
	}
	Ŧ Input::FindId()Ι->optional<T>{
		auto id = FindPtr<jvalue>( "id" );
		if( !id )
			return {};
		auto n = id->try_to_number<T>();
		return n ? optional<T>{*n} : optional<T>{};
	}

	Ŧ Input::Id()Ι->T{
		const optional<T> y = FindId<T>();
		ASSERT( y );
		return y.value_or( T{} );
	}
}
#undef let
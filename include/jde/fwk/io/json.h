#pragma once
#ifndef JSON_H
#define JSON_H
#include <expected>
#include <jde/fwk/exceptions/CodeException.h>

#define Φ Γ α
namespace Jde{
	Φ operator<( const jvalue& a, const jvalue& b )ι->bool;
	Ξ operator<=( const jvalue& a, const jvalue& b )ι->bool{ return a<b || a==b; }
	Ξ operator>( const jvalue& a, const jvalue& b )ι->bool{ return b<a; }
	Ξ operator>=( const jvalue& a, const jvalue& b )ι->bool{ return a>b || a==b; }
	//A jobject overload's path is a bare key or a '/'-separated path, the leading '/' optional - see FindValue.  A jvalue overload's is a strict JSON pointer.
	namespace Json{
		Ŧ Eval( boost::system::result<T> x, std::invocable auto&& message, SRCE )ε->T;//message is called only on failure.
		Φ Visit( const jvalue& v, function<void(const jvalue& o)> op )ε->void;
		Φ Visit( const jvalue& v, function<void(sv s)> op )ε->void;
		Φ Visit( jvalue& v, function<void(jobject& o)> op )ε->void;
		Φ TryReadJsonNet( const fs::path& path, const vector<fs::path>& importPaths={}, const flat_map<string,string>& extVars={}, const flat_map<string,string>& tlaCodeVars={}, SRCE )ι->std::expected<jobject,string>;
		Φ ReadJsonNet( const fs::path& path, const vector<fs::path>& importPaths={}, const flat_map<string,string>& extVars={}, const flat_map<string,string>& tlaCodeVars={}, SRCE )ε->jobject;
		constexpr sv errorFormat = "'{}' could not convert to {}.";
#define $(type) Eval( v.try_as_##type(), [&]{ return Ƒ(errorFormat, serialize(v), #type); }, sl )
		Φ AsValue( const jvalue& v, sv path, SRCE )ε->const jvalue&;
		Φ AsValue( const jobject& o, sv path, SRCE )ε->const jvalue&;
		Ξ AsArray( jvalue& v, SRCE )ε->jarray&{ return $(array); }
		Ξ AsArray( const jvalue& v, SRCE )ε->const jarray&{ return $(array); }
		Ξ AsArray( const jobject& o, sv path, SRCE )ε->const jarray&{ return AsArray( AsValue(o,path,sl), sl ); }
		Ξ AsBool( const jvalue& v, SRCE )ε->bool{ return $(bool); }
		Ξ AsBool( const jobject& o, sv path, SRCE )ε->bool{ return AsBool( AsValue(o,path,sl), sl ); }
		Ŧ AsNumber( const jvalue& v, SRCE )ε->T;
		Ŧ AsNumber( const jobject& o, sv path, SRCE )ε->T{ return AsNumber<T>( AsValue(o,path,sl), sl ); }
		Ξ AsSV( const jvalue& v, SRCE )ε->sv{ return $(string); }
		Ξ AsSV( const jobject& o, sv path, SRCE )ε->sv{ return AsSV( AsValue(o,path,sl), sl ); }
		Ξ AsString( const jvalue& v, SRCE )ε->string{ return string{ $(string) }; }
		Ξ AsString( const jobject& o, sv path, SRCE )ε->string{ return string{AsSV(o,path,sl)}; }
		Ξ AsObject( jvalue& v, SRCE )ε->jobject&{ return $(object); }
		Ξ AsObject( const jvalue& v, SRCE )ε->const jobject&{ return $(object); }
		Ξ AsObject( const jvalue& v, sv path, SRCE )ε->const jobject&{ return AsObject( AsValue(v,path,sl), sl ); }
		Ξ AsObject( jobject& o, sv path, SRCE )ε->jobject&{ return AsObject( const_cast<jvalue&>(AsValue(o,path,sl)), sl ); }
		Ξ AsObject( const jobject& o, sv path, SRCE )ε->const jobject&{ return AsObject( AsValue(o,path,sl), sl ); }

#undef $
		Ξ FindValue( const jvalue& v, sv path )ι->const jvalue*{ auto y = v.try_at_pointer(path); return y.has_value() ? &*y : nullptr; }//a pointer into v, like the jobject overload - it used to return optional<jvalue>, a deep copy of the subtree.
		Φ FindValue( jobject& o, sv path )ι->jvalue*;
		Ξ FindValue( const jobject& o, sv path )ι->const jvalue*{ jvalue* v = FindValue(const_cast<jobject&>(o), path); return v; }
		Ξ FindArray( const jvalue& v, sv path )ι->const jarray*{ auto p = FindValue(v,path); return p ? p->if_array() : nullptr; }
		Ξ FindArray( const jobject& o, sv path )ι->const jarray*{ auto p = FindValue(o,path); return p ? p->if_array() : nullptr; }
		Ξ FindBool( const jvalue& v, sv path )ι->optional<bool>{ auto p = FindValue(v,path); return p && p->is_bool() ? p->get_bool() : optional<bool>{}; }
		Ξ FindBool( const jobject& o, sv path )ι->optional<bool>{ auto p = FindValue(o,path); return p && p->is_bool() ? p->get_bool() : optional<bool>{}; }
		Ξ FindObject( const jvalue& v, sv path )ι->const jobject*{ auto p = FindValue(v,path); return p ? p->if_object() : nullptr; }
		Ξ FindSV( const jvalue& v, sv path )ι->optional<sv>{ auto p = FindValue(v,path); return p && p->is_string() ? p->get_string() : optional<sv>{}; }
		Ξ FindSV( const jobject& o, sv path )ι->optional<sv>{ auto p = FindValue(o,path); return p && p->is_string() ? p->get_string() : optional<sv>{}; }

		Ξ FindString( const jvalue& v, sv path )ι->optional<string>{ auto p = FindSV(v,path); return p ? string{*p} : optional<string>{}; }
		Ξ FindString( const jobject& o, sv path )ι->optional<string>{ auto p = FindSV(o,path); return p ? string{*p} : optional<string>{}; }
		Ŧ FindNumber( const jvalue& v, sv path )ι->optional<T>;
		template<IsEnum T, class ToEnum> α FindEnum( const jobject& o, sv path, ToEnum&& toEnum )ι->optional<T>;
		template<IsEnum T, class ToEnum> α FindEnum( const jvalue& v, sv path, ToEnum&& toEnum )ι->optional<T>;

		Φ FindDefaultArray( const jvalue& v, sv path )ι->const jarray&;
		Φ FindDefaultArray( const jobject& o, sv path )ι->const jarray&;
		Φ FindDefaultObject( const jvalue& v, sv path )ι->const jobject&;
		Φ FindDefaultObject( const jobject& o, sv path )ι->const jobject&;

		Ŧ FindNumber( const jobject& o, sv path )ι->optional<T>;
		Ξ FindObject( const jobject& o, sv path )ι->const jobject*{ auto p = FindValue(o,path); return p ? p->if_object() : nullptr; }
		Ξ FindObject( jobject& o, sv path )ι->jobject*{ auto p = FindValue(o,path); return p ? p->if_object() : nullptr; }
		Φ FindDuration( const jobject& o, sv path, ELogLevel level=ELogLevel::Debug, SRCE )ι->optional<Duration>;
		Φ FindTimePoint( const jobject& o, sv path )ι->optional<TimePoint>;
		Φ FindTimeZone( const jobject& o, sv path, const std::chrono::time_zone& dflt, ELogLevel level=ELogLevel::Debug, SRCE )ι->const std::chrono::time_zone&;

		Ξ FindDefaultSV( const jobject& o, sv path )ι->sv{ auto p = FindSV(o,path); return p ? *p : sv{}; }
		Ξ FindDefaultSV( const jvalue& v, sv path )ι->sv{ auto p = FindSV(v,path); return p ? *p : sv{}; }

		Φ Parse( sv json, SRCE )ε->jobject;
		Φ ParseValue( sv json, SRCE )ε->jvalue;
		Ŧ FromArray( const jarray& a, SRCE )ε->vector<T>;
	}

	Ŧ Json::AsNumber( const jvalue& v, SL sl )ε->T{
		return Eval( v.try_to_number<T>(), [&]{ return Ƒ(errorFormat, serialize(v), "number"); }, sl );
	}

	Ŧ Json::FindNumber( const jvalue& v, sv path )ι->optional<T>{
		const jvalue* p = FindValue( v, path );
		optional<T> y;
		if( p ){
			if( auto n = p->try_to_number<T>(); n )
				y = *n;
		}
		return y;
	}

	template<IsEnum T, class ToEnum> α Json::FindEnum( const jvalue& v, sv path, ToEnum&& toEnum )ι->optional<T>{
		auto p = FindSV( v, path );
		return p ? toEnum( *p ) : optional<T>{};
	}
	template<IsEnum T, class ToEnum> α Json::FindEnum( const jobject& o, sv path, ToEnum&& toEnum )ι->optional<T>{
		auto p = FindSV( o, path );
		return p ? toEnum( *p ) : optional<T>{};
	}
	Ŧ Json::FindNumber( const jobject& o, sv path )ι->optional<T>{
		optional<T> y;
		if( auto m = FindValue(o, path); m )
			if( auto v = m->try_to_number<T>(); v )
				y = *v;
		return y;
	}
	Ŧ Json::FromArray( const jarray& a, SL sl )ε->vector<T>{
		vector<T> y; y.reserve( a.size() );
		for( uint i=0; i<a.size(); ++i )
			y.push_back( Eval(boost::json::try_value_to<T>(a[i]), [&]{ return Ƒ("Element {}, '{}', could not convert.", i, serialize(a[i])); }, sl) );
		return y;
	}
}
Ŧ Jde::Json::Eval( boost::system::result<T> x, std::invocable auto&& message, SL sl )ε->T{
	if( !x )
		throw CodeException{ x.error(), ELogTags::Parsing, message(), ELogLevel::Debug, sl };
	return *std::move( x );//moves a value out; a reference result hands back its reference.
}
#undef Φ
#endif
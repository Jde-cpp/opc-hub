#include "jde/fwk/exceptions/CodeException.h"
#ifdef BOOST_ALL_NO_LIB
	#include <boost/json/src.hpp>
#endif
#include <jde/fwk/io/json.h>
#pragma warning( disable : 4996 )
#include <libjsonnet++.h>
#include <jde/fwk/str.h>
#include <jde/fwk/chrono.h>

#define let const auto
namespace Jde{
	jarray _emptyArray;
	jobject _emptyObject;

	α Json::AppendOrAssign( jvalue& objOrArray, jvalue&& item, SL sl )ε->void{
		if( objOrArray.is_array() )
			objOrArray.get_array().push_back( move(item) );
		else if( objOrArray.is_object() )
			objOrArray = move(item);
		else
			THROWSL( "'{}' is not an array or object.", Kind(objOrArray.kind()) );
	}
	α Json::Visit( const jvalue& v, function<void(sv s)> op )ε->void{
		if( v.is_array() ){
			for( auto& value : v.get_array() ){
				THROW_IF( !value.is_string(), "Expected string but found '{}'.", Kind(value.kind()) );
				op( value.get_string() );
			}
		}
		else{
			THROW_IF( !v.is_string(), "Expected string but found '{}'.", Kind(v.kind()) );
			op( v.get_string() );
		}
	}
	α Json::Visit( jvalue&& v, function<void(jobject&& o)> op )ε->void{
		if( v.is_object() )
			op( move(v.get_object()) );
		else if( v.is_array() ){
			for( auto&& value : v.get_array() )
				op( move(value.get_object()) );
		}
	}
	α Json::Visit( const jvalue& v, function<void(const jvalue& o)> op )ε->void{
		if( v.is_array() ){
			for( let& value : v.get_array() )
				op( value );
		}
		else
			op( v );
	}

	α Json::Visit( jvalue& v, function<void(jobject& o)> op )ε->void{
		if( v.is_array() ){
			for( auto& value : v.get_array() ){
				THROW_IF( !value.is_object(), "Expected object but found '{}'.", Kind(value.kind()) );
				op( value.get_object() );
			}
		}
		else{
			THROW_IF( !v.is_object(), "Expected object but found '{}'.", Kind(v.kind()) );
			op( v.get_object() );
		}
	}

	α Json::AsKey( const jobject& o, SL sl )ε->DB::Key{
		if( auto p = o.if_contains("id"); p )
			return DB::Key{ p->to_number<uint>() };

		if( auto p = o.if_contains("slug"); p )
			return DB::Key{ string{p->as_string()} };
		THROWSL( "Could not find 'id' or 'slug' in {}.", serialize(o) );
	}

	α Json::TryReadJsonNet( const fs::path& path, const vector<fs::path>& importPaths, const flat_map<string,string>& extVars, const flat_map<string,string>& tlaCodeVars, SL sl )ι->std::expected<jobject, string>{
			jsonnet::Jsonnet vm;
			if( !vm.init() )
				return std::unexpected{ Ƒ("Failed to initialize jsonnet VM for '{}'", path.string()) };
			for( let& importPath : importPaths )
				vm.addImportPath( importPath.string() );
			for( let& [key, value] : extVars )
				vm.bindExtVar( key, value );
			for( let& [key, value] : tlaCodeVars )
				vm.bindTlaCodeVar( key, value ); //code, not str: `true` must manifest as a bool - see json.h.
			string j;
			let success = vm.evaluateFile( path.string(), &j );
			if( !success )
				return std::unexpected{ Ƒ("Failed to evaluate '{}'.  {}", path.string(), vm.lastError()) };
		try{
			return Parse( j, sl );
		}
		catch( CodeException& e ){
			return std::unexpected{ Ƒ("Failed to read '{}': {}", path.string(), e.what()) };
		}
	}
	α Json::ReadJsonNet( const fs::path& path, const vector<fs::path>& importPaths, const flat_map<string,string>& extVars, const flat_map<string,string>& tlaCodeVars, SL sl )ε->jobject{
		let json = TryReadJsonNet( path, importPaths, extVars, tlaCodeVars, sl );
		THROW_IFSL( !json, "Failed to evaluate '{}'.  {}", path.string(), json.error() );
		return *json;
	}

	α Json::Parse( sv json, SL sl )ε->jobject{
		return AsObject( ParseValue(json, sl), sl );
	}

	α Json::ParseValue( sv json, SL sl )ε->jvalue{
		std::error_code ec;
		auto value = boost::json::parse( json, ec );
		if( ec )
			throw CodeException{ ec, ELogTags::Parsing, Ƒ("Failed to parse '{}'.", json), ELogLevel::Debug, sl };
		return value;
	}

	α Json::AsArray( jvalue& v, SL sl )ε->jarray&{
		auto y = v.try_as_array();
		if( !y )
			throw CodeException{ y.error(), ELogTags::Parsing, Ƒ("'{}', is not an array but is a '{}'.", serialize(v), Kind(v.kind())), ELogLevel::Debug, sl };
		return *y;
	}

	α Json::AsArray( const jobject& o, sv key, SL sl )ε->const jarray&{
		let& y = o.try_at( key );
		if( !y )
			throw CodeException{ y.error(), ELogTags::Parsing, Ƒ("Key '{}' not found in {}.", key, serialize(o)), ELogLevel::Debug, sl };
		return AsArray( *y, sl );
	}
	α Json::AsArrayPath( const jobject& o, sv path, SL sl )ε->const jarray&{
		let& v = AsValue( o, path, sl );
		return AsArray( v, sl );
	}

	α Json::AsObject( jvalue& v, SL sl )ε->jobject&{
		if( !v.is_object() ){
			let y = v.try_as_object();
			std::error_code ec = y.error();
			let code = ec.value();
			let message = ec.message();
			throw CodeException{ y.error(), ELogTags::Parsing, Ƒ("({})'{}', is not an object but is a '{}'. - {}", code, serialize(v), Kind(v.kind()), message), ELogLevel::Debug, sl };
		}
		return v.get_object();
	}
	α Json::AsObject( const jvalue& v, sv path, SL sl )ε->const jobject&{
		auto p = FindValue( v, path ); THROW_IFSL( !p, "Path '{}' not found in '{}'", path, serialize(v) );
		return AsObject( *p, sl );
	}
	α Json::AsObject( jobject& o, sv key, SL sl )ε->jobject&{
		auto y = o.try_at( key );
		if( !y )
			throw CodeException{ y.error(), ELogTags::Parsing, Ƒ("Key '{}' not found in {}.", key, serialize(o)), ELogLevel::Debug, sl };
		return AsObject( *y, sl );
	}
	α Json::AsObjectPath( const jobject& o, sv path, SL sl )ε->const jobject&{
		let& v = FindValue( o, path );
		THROW_IFSL( !v || !v->is_object(), "object '{}' not found in '{}'.", path, serialize(o) );
		return v->get_object();
	}
	α Json::AsTimePoint( const jobject& o, sv key, SL sl )ε->TimePoint{
		auto p = FindValue( o, key );
		THROW_IFSL( !p || !p->is_string(), "Key '{}' not found in '{}'.", key, serialize(o) );
		return Chrono::ToTimePoint( string{p->get_string()} );
	}

	//the three FindDefaultObject spellings differ only in how they address the value - a key, a path on an object, a path on a
	//value - so the object-or-empty answer is written once.
	Ω objectOrEmpty( const jvalue* v )ι->const jobject&{ return v && v->is_object() ? v->get_object() : _emptyObject; }
	α Json::FindDefaultObject( const jobject& o, sv key )ι->const jobject&{ return objectOrEmpty( o.if_contains(key) ); }
	α Json::FindDefaultObjectPath( const jobject& o, sv path )ι->const jobject&{ return objectOrEmpty( FindValue(o, path) ); }
	α Json::FindDefaultObject( const jvalue& v, sv path )ι->const jobject&{ return objectOrEmpty( FindValue(v, path) ); }
	α Json::AsValue( const jobject& o, sv path, SL sl )ε->const jvalue&{
		auto p = FindValue( o, path ); THROW_IFSL( !p, "Path '{}' not found in '{}'.", path, serialize(o) );
		return *p;
	}
	α Json::AsSV( const jobject& o, sv key, SL sl )ε->sv{
		auto p = FindSV( o, key );
		THROW_IFSL( !p, "String key '{}' not found in '{}'.", key, serialize(o) );
		return *p;
	}
	α Json::AsSVPath( const jobject& o, sv path, SL sl )ε->sv{
		auto p = FindSVPath( o, path );
		THROW_IFSL( !p, "Path '{}' not found in '{}'.", path, serialize(o) );
		return *p;
	}
	α Json::FindString( const jobject& o, sv key )ι->optional<string>{
		let y = FindSV( o, key );
		return y ? string{ *y } : optional<string>{};
	}

	α Json::FindDuration( const jobject& o, sv key, ELogLevel level, SL sl )ι->optional<Duration>{
		let v = o.if_contains( key );
		if( !v || !v->is_string() )
			return {};
		let d = Chrono::TryToDuration( string{v->get_string()}, level, sl );
		return d ? optional<Duration>{ *d } : nullopt;
	}

	α Json::FindTimePoint( const jobject& o, sv key )ι->optional<TimePoint>{
		let v = o.if_contains( key );
		return v && v->is_string() ? Chrono::ToTimePoint( string{v->get_string()} ) : optional<TimePoint>{};
	}

	α Json::FindTimeZone( const jobject& o, sv key, const std::chrono::time_zone& dflt, ELogLevel level, SL sl )ι->const std::chrono::time_zone&{
		auto p = FindValue( o, key );
		if( p && p->is_string() )
			return Chrono::ToTimeZone( string{p->get_string()}, dflt, level, sl );
		return dflt;
	}

	α Json::FindSVPath( const jobject& o, sv path )ι->optional<sv>{
		auto p = FindValue( o, path );
		return p && p->is_string() ? p->get_string() : optional<sv>{};
	}

	//json-pointer semantics, same engine as the jvalue overloads; tolerates the legacy form without the leading '/'.
	//jobject has no try_at_pointer - resolve the first token, then delegate the tail to value::try_at_pointer.
	α Json::FindValue( jobject& o, sv path )ι->jvalue*{
		if( path.starts_with('/') )
			path.remove_prefix( 1 );
		let iNext = path.find( '/' );
		auto p = o.if_contains( path.substr(0, iNext) );
		if( !p || iNext==sv::npos )
			return p;
		auto y = p->try_at_pointer( path.substr(iNext) );//substr keeps the '/' the pointer syntax requires.
		return y.has_value() ? &*y : nullptr;
	}

	α Json::FindArray( const jvalue& v, sv path )ι->const jarray*{
		auto value = v.try_at_pointer( path );
		return value ? value->if_array() : nullptr;
	}
	α Json::FindArray( const jobject& o, sv key )ι->const jarray*{
		let& value = o.try_at( key );
		return value ? value->if_array() : nullptr;
	}
	α Json::FindDefaultArray( const jvalue& v, sv path )ι->const jarray&{
		auto p = FindArray( v, path );
		return p ? *p : _emptyArray;
	}
	α Json::FindDefaultArray( const jobject& o, sv key )ι->const jarray&{
		let y = FindArray( o, key );
		return y ? *y : _emptyArray;
	}
	α Json::FindBool( const jobject& o, sv key )ι->optional<bool>{
		let value = o.if_contains( key );
		return value && value->is_bool() ? value->get_bool() : optional<bool>{};
	}
	α Json::Kind( boost::json::kind kind )ι->string{
		string y;
		switch( kind ){
			using enum boost::json::kind;
			case null: y = "null"; break;
			case string: y = "string"; break;
			case bool_: y = "bool"; break;
			case int64: y = "int"; break;
			case uint64: y = "uint"; break;
			case double_: y = "double"; break;
			case object: y = "object"; break;
			case array: y = "array"; break;
			default: y = "unknown";
		}
		return y;
	}

	//used to find a database value in a ql filter.
	α Json::Find( const jvalue& container, const jvalue& item )ι->const jvalue*{
		auto y = container.is_primitive() && item.is_primitive() && container==item ? &item : nullptr;
		if( let array = y || !container.is_array() ? nullptr : &container.get_array(); array ){
			for( auto p = array->begin(); !y && p!=array->end(); ++p )
				y = p->is_primitive() && item==*p ? &*p : nullptr;
		}
		return y;
	}
	α Json::Combine( const jobject& a, const jobject& b )ι->jobject{
		jobject y = a;
		for( let& [key,bValue] : b ){
			if( auto yValue = y.if_contains(key); yValue ){
				if( let yObject = bValue.is_object() && yValue->is_object() ? &yValue->get_object() : nullptr; yObject )
					y[key] = Combine( *yObject, bValue.get_object() );
				else if( let yArray = yValue->is_array() && bValue.is_array() ? &yValue->get_array() : nullptr; yArray ){
					for( let& item : bValue.get_array() ){
						if( find_if( *yArray, [&](const jvalue& v){return v==item;})==yArray->end() )
							yArray->emplace_back( item );
					}
				}
			}
			else
				y[key] = bValue;
		}
		return y;
	}
}
α Jde::operator<( const jvalue& a, const jvalue& b )ι->bool{
	bool less{};
	if( a.is_primitive() && b.is_primitive() ){
		if( a.is_string() && b.is_string() )
			less = a.get_string() < b.get_string();
		else if( a.is_double() && b.is_double() )
			less = a.get_double() < b.get_double();
		else if( a.is_number() && b.is_number() ){//to_number<uint> throws for negative/fractional values - this is noexcept.
			if( a.is_int64() && b.is_int64() )
				less = a.get_int64() < b.get_int64();
			else if( a.is_uint64() && b.is_uint64() )
				less = a.get_uint64() < b.get_uint64();
			else if( a.is_int64() && b.is_uint64() )
				less = a.get_int64()<0 || (uint64_t)a.get_int64() < b.get_uint64();
			else if( a.is_uint64() && b.is_int64() )
				less = b.get_int64()>=0 && a.get_uint64() < (uint64_t)b.get_int64();
			else{//at least one double
				boost::system::error_code e1, e2;
				let x = a.to_number<double>( e1 ); let y = b.to_number<double>( e2 );
				less = !e1 && !e2 && x<y;
			}
		}
		else if( a.is_bool() && b.is_bool() )
			less = a.get_bool() < b.get_bool();
	}
	return less;
}
#pragma warning( disable : 4996 )
#include <jde/fwk/io/json.h>
#ifdef BOOST_ALL_NO_LIB
	#include <boost/json/src.hpp>
#endif
#include <libjsonnet++.h>
#include <jde/fwk/chrono.h>

#define let const auto
namespace Jde{
	jarray _emptyArray;
	jobject _emptyObject;

	α Json::Visit( const jvalue& v, function<void(sv s)> op )ε->void{
		if( v.is_array() ){
			for( auto& value : v.get_array() ){
				THROW_IF( !value.is_string(), "Expected string but found '{}'.", to_string(value.kind()) );
				op( value.get_string() );
			}
		}
		else{
			THROW_IF( !v.is_string(), "Expected string but found '{}'.", to_string(v.kind()) );
			op( v.get_string() );
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
				THROW_IF( !value.is_object(), "Expected object but found '{}'.", to_string(value.kind()) );
				op( value.get_object() );
			}
		}
		else{
			THROW_IF( !v.is_object(), "Expected object but found '{}'.", to_string(v.kind()) );
			op( v.get_object() );
		}
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

	//the FindDefaultObject spellings differ only in how they address the value - a path on an object or on a value - so the
	//object-or-empty answer is written once.
	Ω objectOrEmpty( const jvalue* v )ι->const jobject&{ return v && v->is_object() ? v->get_object() : _emptyObject; }
	α Json::FindDefaultObject( const jobject& o, sv path )ι->const jobject&{ return objectOrEmpty( FindValue(o, path) ); }
	α Json::FindDefaultObject( const jvalue& v, sv path )ι->const jobject&{ return objectOrEmpty( FindValue(v, path) ); }
	α Json::AsValue( const jvalue& v, sv path, SL sl )ε->const jvalue&{
		auto p = FindValue( v, path ); THROW_IFSL( !p, "Path '{}' not found in '{}'.", path, serialize(v) );
		return *p;
	}
	α Json::AsValue( const jobject& o, sv path, SL sl )ε->const jvalue&{
		auto p = FindValue( o, path ); THROW_IFSL( !p, "Path '{}' not found in '{}'.", path, serialize(o) );
		return *p;
	}

	α Json::FindDuration( const jobject& o, sv path, ELogLevel level, SL sl )ι->optional<Duration>{
		let v = FindValue( o, path );
		if( !v || !v->is_string() )
			return {};
		let d = Chrono::TryToDuration( v->get_string(), level, sl );
		return d ? optional<Duration>{ *d } : nullopt;
	}

	α Json::FindTimePoint( const jobject& o, sv path )ι->optional<TimePoint>{
		let v = FindValue( o, path );
		return v && v->is_string() ? Chrono::ToTimePoint( string{v->get_string()} ) : optional<TimePoint>{};
	}

	α Json::FindTimeZone( const jobject& o, sv path, const std::chrono::time_zone& dflt, ELogLevel level, SL sl )ι->const std::chrono::time_zone&{
		auto p = FindValue( o, path );
		if( p && p->is_string() )
			return Chrono::ToTimeZone( string{p->get_string()}, dflt, level, sl );
		return dflt;
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

	α Json::FindDefaultArray( const jvalue& v, sv path )ι->const jarray&{ auto p = FindArray(v, path); return p ? *p : _emptyArray; }
	α Json::FindDefaultArray( const jobject& o, sv path )ι->const jarray&{ auto p = FindArray(o, path); return p ? *p : _emptyArray; }
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
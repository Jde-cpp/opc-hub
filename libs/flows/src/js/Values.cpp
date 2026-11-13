#include "Values.h"

#define let const auto

namespace Jde::Opc::Flows{
	constexpr int64_t _safe{ (1ll<<53)-1 };//Number.MAX_SAFE_INTEGER.
	constexpr uint _depth{ 64 };

	Ω spend( JSContext* ctx, uint& budget, uint bytes )ι->bool{
		if( bytes>budget ){
			JS_ThrowRangeError( ctx, "The value is larger than `function.memory`." );
			return false;
		}
		budget -= bytes;
		return true;
	}
	Ω bigInt( JSContext* ctx, JSValueConst v, jvalue& y )ι->bool{
		let reads = [&]( JSValue back ){//takes `back`.
			let same = !JS_IsException( back ) && JS_IsStrictEqual( ctx, back, v );
			JS_FreeValue( ctx, back );
			return same;
		};
		int64_t i{};
		uint64_t u{};
		if( JS_ToBigInt64(ctx, &i, v) || JS_ToBigUint64(ctx, &u, v) )
			return false;
		if( reads(JS_NewBigInt64(ctx, i)) )
			y = i;
		else if( !JS_HasException(ctx) && reads(JS_NewBigUint64(ctx, u)) )
			y = u;
		else{
			if( !JS_HasException(ctx) )
				JS_ThrowRangeError( ctx, "A BigInt past 64 bits does not cross into a message." );
			return false;
		}
		return true;
	}

	//`absent`:  set for what JSON.stringify leaves out, a function, a symbol and `undefined`.  `plain`:  the value is what
	//a `toJSON` answered, which is not asked again.
	Ω toJson( JSContext* ctx, JSValueConst v, jvalue& y, uint& budget, uint depth, bool& absent, bool plain=false )ι->bool;
	Ω members( JSContext* ctx, JSValueConst v, jvalue& y, uint& budget, uint depth )ι->bool{
		if( JS_IsArray(v) ){
			int64_t count{};
			if( JS_GetLength(ctx, v, &count)<0 )
				return false;
			auto& a = y.emplace_array();
			for( int64_t i=0; i<count; ++i ){
				JSValue item = JS_GetPropertyInt64( ctx, v, i );
				if( JS_IsException(item) )
					return false;
				jvalue element;
				bool absent{};
				let read = toJson( ctx, item, element, budget, depth+1, absent );
				JS_FreeValue( ctx, item );
				if( !read )
					return false;
				a.push_back( absent ? jvalue{} : move(element) );
			}
			return true;
		}
		JSPropertyEnum* names{};
		uint32_t count{};
		if( JS_GetOwnPropertyNames(ctx, &names, &count, v, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)<0 )
			return false;
		auto& o = y.emplace_object();
		bool read{ true };
		for( uint32_t i=0; read && i<count; ++i ){
			JSValue member = JS_GetProperty( ctx, v, names[i].atom );
			jvalue value;
			bool absent{};
			read = !JS_IsException( member ) && toJson( ctx, member, value, budget, depth+1, absent );
			JS_FreeValue( ctx, member );
			if( !read || absent )
				continue;
			size_t size{};
			let key = JS_AtomToCStringLen( ctx, &size, names[i].atom );
			read = key && spend( ctx, budget, size );
			if( read )
				o.insert_or_assign( sv{key, size}, move(value) );
			if( key )
				JS_FreeCString( ctx, key );
		}
		JS_FreePropertyEnum( ctx, names, count );
		return read;
	}
	Ω toJson( JSContext* ctx, JSValueConst v, jvalue& y, uint& budget, uint depth, bool& absent, bool plain )ι->bool{
		if( !spend(ctx, budget, sizeof(jvalue)) )
			return false;
		if( JS_IsUndefined(v) || JS_IsSymbol(v) || JS_IsFunction(ctx, v) )
			absent = true;
		else if( JS_IsNull(v) )
			y = nullptr;
		else if( JS_IsBool(v) )
			y = JS_ToBool( ctx, v )>0;
		else if( JS_IsBigInt(v) )
			return bigInt( ctx, v, y );
		else if( JS_IsNumber(v) ){
			double d{};
			JS_ToFloat64( ctx, &d, v );
			if( !std::isfinite(d) )
				y = nullptr;
			else if( d==std::trunc(d) && std::abs(d)<=(double)_safe )
				y = (int64_t)d;
			else
				y = d;
		}
		else if( JS_IsString(v) ){
			size_t size{};
			let text = JS_ToCStringLen( ctx, &size, v );
			let read = text && spend( ctx, budget, size );
			if( read )
				y = sv{ text, size };
			if( text )
				JS_FreeCString( ctx, text );
			return read;
		}
		else if( JS_IsObject(v) ){
			if( depth>=_depth ){
				JS_ThrowTypeError( ctx, "The value is nested past %u levels, as one that holds itself is.", (unsigned)_depth );
				return false;
			}
			JSValue asked = plain ? JS_UNDEFINED : JS_GetPropertyStr( ctx, v, "toJSON" );
			if( !JS_IsFunction(ctx, asked) ){
				let failed = JS_IsException( asked );
				JS_FreeValue( ctx, asked );
				return !failed && members( ctx, v, y, budget, depth );
			}
			JSValue made = JS_Call( ctx, asked, v, 0, nullptr );
			JS_FreeValue( ctx, asked );
			let read = !JS_IsException( made ) && toJson( ctx, made, y, budget, depth+1, absent, true );
			JS_FreeValue( ctx, made );
			return read;
		}
		else
			absent = true;
		return true;
	}
}
namespace Jde::Opc{
	α Flows::Js::ToJs( JSContext* ctx, const jobject& o )ι->JSValue{
		JSValue y = JS_NewObject( ctx );
		if( JS_IsException(y) )
			return y;
		for( let& member : o ){
			let key = JS_NewAtomLen( ctx, member.key().data(), member.key().size() );
			JSValue value = key==JS_ATOM_NULL ? JS_EXCEPTION : ToJs( ctx, member.value() );
			let defined = !JS_IsException( value ) && JS_DefinePropertyValue( ctx, y, key, value, JS_PROP_C_W_E )>=0;
			if( key!=JS_ATOM_NULL )
				JS_FreeAtom( ctx, key );
			if( !defined ){
				JS_FreeValue( ctx, y );
				return JS_EXCEPTION;
			}
		}
		return y;
	}
	α Flows::Js::ToJs( JSContext* ctx, const jvalue& v )ι->JSValue{
		switch( v.kind() ){
		case boost::json::kind::null: return JS_NULL;
		case boost::json::kind::bool_: return JS_NewBool( ctx, v.get_bool() );
		case boost::json::kind::int64:{
			let i = v.get_int64();
			return i>=-_safe && i<=_safe ? JS_NewInt64( ctx, i ) : JS_NewBigInt64( ctx, i );
		}
		case boost::json::kind::uint64:{
			let u = v.get_uint64();
			return u<=(uint64_t)_safe ? JS_NewInt64( ctx, (int64_t)u ) : JS_NewBigUint64( ctx, u );
		}
		case boost::json::kind::double_: return JS_NewFloat64( ctx, v.get_double() );
		case boost::json::kind::string: return JS_NewStringLen( ctx, v.get_string().data(), v.get_string().size() );
		case boost::json::kind::array:{
			JSValue y = JS_NewArray( ctx );
			uint32_t i{};
			for( let& item : v.get_array() ){
				if( JS_IsException(y) )
					break;
				JSValue element = ToJs( ctx, item );
				if( JS_IsException(element) || JS_DefinePropertyValueUint32(ctx, y, i++, element, JS_PROP_C_W_E)<0 ){
					JS_FreeValue( ctx, y );
					y = JS_EXCEPTION;
				}
			}
			return y;
		}
		case boost::json::kind::object: return ToJs( ctx, v.get_object() );
		}
		return JS_UNDEFINED;
	}
	α Flows::Js::ToJson( JSContext* ctx, JSValueConst v, jvalue& y, uint budget )ι->bool{
		bool absent{};
		if( !toJson(ctx, v, y, budget, 0, absent) )
			return false;
		if( absent )
			y = nullptr;
		return true;
	}
}
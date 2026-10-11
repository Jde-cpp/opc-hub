#include <jde/db/Value.h>
#include <boost/uuid/string_generator.hpp>
#include <jde/fwk/chrono.h>
#include <jde/db/usings.h>
#include <jde/db/generators/Syntax.h>


#define let const auto

namespace Jde::DB{
	using namespace Json;
	constexpr ELogTags _tags{ ELogTags::Sql };
	constexpr array<sv,11> EValueStrings = { "null", "string", "bool", "int8", "int32", "int64", "uint32", "uint64", "double", "time", "bytes" };

	Ω fromJson( EType type, const jvalue& j, SL sl )->Value::Underlying{
		Value::Underlying value{ nullptr };
		if( j.is_null() )
			return value;

		switch( type ){
			using enum EType;
		case VarWChar: case VarChar: case NText: case Text: case Uri: value = AsString( j ); break;
		case Bit: value = AsBool(j, sl); break;
		case Int8: value = AsNumber<int8>(j, sl); break;
		case Int16: case Int: value = AsNumber<int>( j ); break;
		case Long: value = AsNumber<_int>( j ); break;
		case UInt8: value = AsNumber<uint8>( j ); break;
		case UInt16: case UInt: value = AsNumber<uint32_t>( j ); break; //exact-width: the fast typedefs are 64-bit on glibc and would land on the uint64 alternative.
		case ULong: value = AsNumber<uint>( j ); break;
		case SmallFloat: case Float: case Decimal: case Numeric: case Money: value = AsNumber<double>( j ); break;
		case DateTime: case SmallDateTime:{
			auto iso = AsString( j );
			value = iso=="$now" ? Value::Underlying{move(iso)} : Value::Underlying{ DBTimePoint{Chrono::ToTimePoint(move(iso), sl)} };
		} break;
		case Guid:{//its text, as ValueToJson renders it:  the 16 bytes in RFC 4122 order, which each driver stores (binary(16), blob, uniqueidentifier).
			let text = AsString( j );
			try{
				value = Value{ boost::uuids::string_generator{}(text.begin(), text.end()) }.Variant;
			}
			catch( const std::runtime_error& ){
				THROWSL( "'{}' is not a guid.", text );
			}
		} break;
		case None: case Binary: case VarBinary: case Cursor: case RefCursor: case Image: case Blob: case TimeSpan:
			THROW( "EValue {} is not implemented.", (uint)type );
		case WChar: case Char: default:
			THROW( "char EValue {} is not implemented.", (uint)type );
		}
		return value;
	}

	Value::Value( EType type, const jvalue& j, SL sl )ε:
		Variant{ fromJson(type, j, sl) }
	{}

	//The three conversions below visit the variant rather than switch on Type(): every alternative must be named or the
	//lambda fails to compile, so a new Underlying member cannot fall through to an ERR/empty default.
	α Value::ToString()Ι->string{
		return std::visit( []<class T>( const T& v )->string{
			if constexpr( std::same_as<T,std::nullptr_t> ) return "null";
			else if constexpr( std::same_as<T,string> ) return v;
			else if constexpr( std::same_as<T,bool> ) return v ? "true" : "false";
			else if constexpr( std::same_as<T,DBTimePoint> ) return ToIsoString( v );
			else if constexpr( std::same_as<T,vector<uint8_t>> ) return Str::Encode64( v );
			else return std::to_string( v ); //the integers and double.
		}, Variant );
	}
	α Value::TypeName()Ι->string{ return FromEnum( EValueStrings, Type() ); }

	α Value::ToUInt()Ι->uint{
		return std::visit( [&]<class T>( const T& v )->uint{
			if constexpr( std::same_as<T,double> )
				return (uint)(_int)v; //#21: through _int - a negative double straight to uint is UB, this wraps like the integers do.
			else if constexpr( std::is_arithmetic_v<T> )
				return (uint)v;
			else{
				if constexpr( !std::same_as<T,std::nullptr_t> ) //Null intentionally 0.
					WARN( "ToUInt on non-numeric type '{}' returns 0.", TypeName() );
				return 0;
			}
		}, Variant );
	}

	α Value::Move()ι->jvalue{
		jvalue y;
		if( Type()==EValue::String )
			y = std::get<string>( move(Variant) );
		else
			ToJson( y );
		return y;
	}

	α Value::ToJson( jvalue& j )Ι->void{
		std::visit( [&]<class T>( const T& v ){
			if constexpr( std::same_as<T,DBTimePoint> ) j = ToIsoString( v ); //ToIsoString already ends in 'Z'.
			else if constexpr( std::same_as<T,vector<uint8_t>> ) j = Str::Encode64( v );
			else j = v; //nullptr, string, bool, the integers, double - jvalue takes each directly.
		}, Variant );
	}
}
namespace Jde{
	//The config's own spellings - common-meta.libsonnet's `int16`/`smallFloat`/`uint*`, the C#-ish `Long`/`ULong`/`bool`/`guid`
	//- then the common table (B3); case-insensitive throughout.
	constexpr std::array<std::pair<sv,DB::EType>,13> ConfigTypeNames{{
		{"bool",DB::EType::Bit}, {"uint",DB::EType::UInt}, {"uint64",DB::EType::ULong}, {"ulong",DB::EType::ULong}, {"long",DB::EType::Long},
		{"int16",DB::EType::Int16}, {"uint16",DB::EType::UInt16}, {"int8",DB::EType::Int8}, {"uint8",DB::EType::UInt8},
		{"guid",DB::EType::Guid}, {"blob",DB::EType::Blob}, {"smallfloat",DB::EType::SmallFloat}, {"uri",DB::EType::Uri}
	}};
	α DB::ToType( sv csTypeName )ι->DB::EType{
		let iv = ToIV( csTypeName );
		if( auto p = find_if(ConfigTypeNames, [&](let& x){ return ToIV(x.first)==iv; }); p!=ConfigTypeNames.end() )
			return p->second;
		let common = DB::FindCommonType( csTypeName );
		if( !common )
			TRACE( "Unknown datatype({}).", csTypeName );
		return common.value_or( DB::EType::None );
	}
}
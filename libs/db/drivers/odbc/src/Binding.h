#pragma once
#include <jde/db/DBException.h>

#define let const auto
namespace Jde::DB::Odbc{
#pragma warning( push )
#pragma warning (disable: 4716)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexceptions"

struct Binding{
		Binding( SQLSMALLINT type, SQLSMALLINT cType, SQLLEN output=0 )ι:
			Output{output},
			CodeType{cType},
			_dbType{ type }
		{}
		virtual ~Binding()=default;
		Ω GetBinding( SQLSMALLINT type )ε->up<Binding>;
		Ω Create( Value parameter )ι->up<Binding>;

		β Data()ι->void* = 0;
		β GetValue()Ι->Value = 0;
#define $ [[noreturn]] β
		$ to_string()Ε->string{ THROW( "to_string not implemented for DBType='{}' CodeType='{}' {}", _dbType, CodeType, "GetTypeName<decltype(this)>()" ); }
		$ GetDouble()Ε->double{ THROW( "{} not implemented for DBType={} CodeType={}", "GetDouble", _dbType, CodeType ); }
		$ GetDateTime()Ε->DBTimePoint{ THROW( "{} not implemented for DBType={} CodeType={}", "GetDateTime", _dbType, CodeType ); }
		α IsNull()Ι->bool{ return GetOutput()==SQL_NULL_DATA; }
		β Size()Ι->SQLULEN{ return 0; }
		β DecimalDigits()Ι->SQLSMALLINT{return 0;}
		β BufferLength()Ι->SQLLEN{return 0;}
		β DBType()Ι->SQLSMALLINT{ return _dbType; }
		SQLLEN Output; //Binding fills in for each row.
		β GetOutput()Ι->SQLLEN{ return Output; }
		SQLSMALLINT CodeType{0};
	private:
		SQLSMALLINT _dbType;
	};
#undef $
#pragma warning( pop )
	template <class T, SQLSMALLINT TSql, SQLSMALLINT TC>
	struct TBinding : Binding{
		TBinding()ι:Binding{TSql,TC}{}
		α Data()ι->void* override{ return &_data; }
	protected:
		T _data;
	};

	struct BindingNull final : Binding{
		BindingNull( SQLSMALLINT type=SQL_VARCHAR )ι:
			Binding{ type, SQL_C_CHAR, SQL_NULL_DATA }
		{}
		α Data()ι->void* override{ return nullptr; }
		α GetValue()Ι->Value override{ return Value{}; }
	};

	struct BindingString final: Binding{
		BindingString( SQLSMALLINT type, SQLLEN size )ι:
			Binding{ type, SQL_C_CHAR, size },
			_buffer{std::make_unique_for_overwrite<char[]>( size )},
			_bufferSize{ size }{
		}
		BindingString( sv value )ι:
			Binding{ SQL_VARCHAR, SQL_C_CHAR, (SQLLEN)value.size() },
			_buffer{ std::make_unique_for_overwrite<char[]>(value.size()) },
			_bufferSize{ (SQLLEN)value.size() }{
			std::copy(value.begin(), value.end(), _buffer.get());
		}
		α Data()ι->void* override{ return _buffer.get(); }
		α GetValue()Ι->DB::Value override{ return IsNull() ? Value{} : Value{to_string()}; }
		α to_string()Ι->string override{
			let output = GetOutput();
			if( !_buffer || output==SQL_NULL_DATA )
				return {};
			let length = output==SQL_NO_TOTAL || output>=_bufferSize //indicator is the untruncated length; driver wrote _bufferSize-1 chars + null terminator.
				? std::max<SQLLEN>( _bufferSize-1, 0 ) : output;
			return string{ _buffer.get(), _buffer.get()+length };
		}
		α BufferLength()Ι->SQLLEN override{ return std::max<SQLLEN>( GetOutput(), 0 ); }
		α Size()Ι->SQLULEN override{ return (SQLULEN)BufferLength(); }
	private:
		up<char[]> _buffer;
		SQLLEN _bufferSize{};
	};

	//SQL Server nchar/nvarchar/ntext (SQL_WCHAR -8, SQL_WVARCHAR -9, SQL_WLONGVARCHAR -10) arrive as UTF-16LE.
	//Decode to UTF-8 rather than binding SQL_C_CHAR (which transcodes to the ANSI code page and turns non-ANSI into '?').
	Ξ Utf16ToUtf8( const char16_t* p, uint n )->string{
		string out; out.reserve( n );
		for( uint i=0; i<n; ++i ){
			char32_t cp = p[i];
			if( cp>=0xD800 && cp<=0xDBFF && i+1<n ){//high surrogate + low surrogate => astral code point
				char32_t lo = p[i+1];
				if( lo>=0xDC00 && lo<=0xDFFF ){ cp = 0x10000 + ((cp-0xD800)<<10) + (lo-0xDC00); ++i; }
			}
			if( cp<0x80 )
				out += (char)cp;
			else if( cp<0x800 ){
				out += (char)(0xC0 | (cp>>6));
				out += (char)(0x80 | (cp&0x3F));
			}
			else if( cp<0x10000 ){
				out += (char)(0xE0 | (cp>>12));
				out += (char)(0x80 | ((cp>>6)&0x3F));
				out += (char)(0x80 | (cp&0x3F));
			}
			else{
				out += (char)(0xF0 | (cp>>18));
				out += (char)(0x80 | ((cp>>12)&0x3F));
				out += (char)(0x80 | ((cp>>6)&0x3F));
				out += (char)(0x80 | (cp&0x3F));
			}
		}
		return out;
	}

	struct BindingWString final: Binding{
		BindingWString( SQLSMALLINT type, SQLLEN charCapacity )ι://charCapacity includes the null terminator.
			Binding{ type, SQL_C_WCHAR },
			_buffer{ std::make_unique_for_overwrite<char16_t[]>( charCapacity ) },
			_charCapacity{ charCapacity }{
		}
		α Data()ι->void* override{ return _buffer.get(); }
		α GetValue()Ι->DB::Value override{ return IsNull() ? Value{} : Value{to_string()}; }
		α to_string()Ι->string override{
			let output = GetOutput();//for SQL_C_WCHAR the indicator is the untruncated length in BYTES (or SQL_NO_TOTAL).
			if( !_buffer || output==SQL_NULL_DATA )
				return {};
			let allocBytes = _charCapacity*(SQLLEN)sizeof(char16_t);
			let dataBytes = output==SQL_NO_TOTAL || output>=allocBytes//truncated => we hold (_charCapacity-1) code units.
				? std::max<SQLLEN>( (_charCapacity-1)*(SQLLEN)sizeof(char16_t), 0 ) : output;
			return Utf16ToUtf8( _buffer.get(), (uint)(dataBytes/(SQLLEN)sizeof(char16_t)) );
		}
		α BufferLength()Ι->SQLLEN override{ return std::max<SQLLEN>( GetOutput(), 0 ); }
		α Size()Ι->SQLULEN override{ return (SQLULEN)BufferLength(); }
	private:
		up<char16_t[]> _buffer;
		SQLLEN _charCapacity{};
	};

	struct BindingBinary final :Binding{
		BindingBinary():Binding{ SQL_VARBINARY, SQL_C_BINARY }{}
		BindingBinary( SQLLEN size ):Binding{ SQL_VARBINARY, SQL_C_BINARY, size==0 ? SQL_NULL_DATA : size },_value{ vector<uint8_t>(size) }{}
		BindingBinary( Value v ):
			Binding{ SQL_VARBINARY, SQL_C_BINARY, v.get_bytes().size()==0 ? SQL_NULL_DATA : (SQLLEN)v.get_bytes().size()  },
			_value{ move(v) }
		{}
		α Data()ι->void* override{ return IsNull() ? nullptr : _value.get_bytes().data(); }
		α GetValue()Ι->Value override{
			if( IsNull() ) //null row: Output==SQL_NULL_DATA - was returning the whole garbage buffer.
				return Value{};
			let& bytes = _value.get_bytes();
			let indicator = GetOutput(); //driver-written fetched length for this row; SQL_NO_TOTAL(<0) => unknown, take the whole buffer.
			let count = indicator<0 || (SQLULEN)indicator>bytes.size() ? bytes.size() : (uint)indicator;
			return count==bytes.size() ? _value : Value{ vector<uint8_t>{ bytes.begin(), bytes.begin()+count } }; //truncate off the column-max padding / stale bytes from a longer prior row.
		}
		α GetOutput()Ι->SQLLEN override{ return Size()==0 ? SQL_NULL_DATA : Binding::GetOutput(); }

		α Size()Ι->SQLULEN override { return _value.get_bytes().size(); }
		Value _value;
	};
	struct BindingGuid final : Binding{
		BindingGuid():Binding{ SQL_GUID, SQL_C_GUID }{}
		BindingGuid( const uuid& v ):
			Binding{ SQL_GUID, SQL_C_GUID, 16 }{
			//boost::uuids::uuid holds 16 bytes in RFC-4122 (big-endian) order; SQLGUID.Data1/2/3 are native-endian integers.
			//Assemble them explicitly (endian-independent) so the stored uniqueidentifier matches the uuid in SSMS / other clients.
			//NB: this changes the on-disk byte order vs the old raw memcpy - GUIDs written by the previous build are stored byte-shuffled and need migration.
			let* b = (const uint8_t*)v.data();
			_guid.Data1 = ((uint32_t)b[0]<<24) | ((uint32_t)b[1]<<16) | ((uint32_t)b[2]<<8) | b[3];
			_guid.Data2 = (uint16_t)((b[4]<<8) | b[5]);
			_guid.Data3 = (uint16_t)((b[6]<<8) | b[7]);
			memcpy( _guid.Data4, b+8, 8 );
		}
		α Data()ι->void* override{ return &_guid; }
		α GetValue()Ι->Value override {
			uuid id; let b = (uint8_t*)id.data();
			b[0]=(uint8_t)(_guid.Data1>>24); b[1]=(uint8_t)(_guid.Data1>>16); b[2]=(uint8_t)(_guid.Data1>>8); b[3]=(uint8_t)_guid.Data1;
			b[4]=(uint8_t)(_guid.Data2>>8);  b[5]=(uint8_t)_guid.Data2;
			b[6]=(uint8_t)(_guid.Data3>>8);  b[7]=(uint8_t)_guid.Data3;
			memcpy( b+8, _guid.Data4, 8 );
			return Value{ id };
		}
		α GetOutput()Ι->SQLLEN override{ return sizeof(SQLGUID); }
		α Size()Ι->SQLULEN override { return sizeof(SQLGUID); }

		SQLGUID _guid;
	};
	struct BindingBit final : Binding{
		BindingBit()ι:BindingBit{ (SQLSMALLINT)SQL_BIT }{}//-7
		BindingBit( SQLSMALLINT type )ι:Binding{ type, SQL_C_BIT }{}
		BindingBit( bool value )ι: Binding{ SQL_CHAR, SQL_C_BIT },_data{value ? '\1' : '\0'}{}
		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data=='\1'}; }
		α Size()Ι->SQLULEN override{return 1;}
		α to_string()Ι->string override{ return _data ? "true" : "false"; }
	private:
		char _data;
	};
	struct BindingInt8 final : Binding{
		BindingInt8( SQLSMALLINT type=SQL_TINYINT )ι: Binding{ type, SQL_C_TINYINT }{}
		BindingInt8( int8_t value )ι: Binding{ SQL_TINYINT, SQL_C_TINYINT },_data{value}{}
		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data}; }

	private:
		int8_t _data;
	};

	struct BindingInt32 final : Binding{
		BindingInt32( SQLSMALLINT type=SQL_INTEGER )ι: Binding{ type, SQL_C_SLONG }{}
		BindingInt32( int value )ι: Binding{ SQL_INTEGER, SQL_C_SLONG },_data{value}{}
		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data}; }
	private:
		int _data;
	};

	struct BindingInt final : Binding{
		BindingInt( SQLSMALLINT type=SQL_C_SBIGINT )ι: Binding{ type, SQL_C_SBIGINT }{}
		BindingInt( _int value )ι: Binding{ SQL_BIGINT, SQL_C_SBIGINT },_data{value}{}
		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data}; }
	private:
		_int _data ;
	};

	struct BindingTimeStamp final:  Binding{
		BindingTimeStamp( SQLSMALLINT type=SQL_C_TYPE_TIMESTAMP )ι: Binding{ type, SQL_C_TYPE_TIMESTAMP }{}
		BindingTimeStamp( SQL_TIMESTAMP_STRUCT value )ι: Binding{ SQL_TYPE_TIMESTAMP, SQL_C_TYPE_TIMESTAMP },_data{value}{}
		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{GetDateTime()}; }
		α GetDateTime()Ι->DBTimePoint override{ return IsNull() ? DBTimePoint{} : Chrono::ToTimePoint( _data.year, (uint8)_data.month, (uint8)_data.day, (uint8)_data.hour, (uint8)_data.minute, (uint8)_data.second, std::chrono::duration_cast<Duration>(std::chrono::nanoseconds(_data.fraction)) ); }
	private:
		SQL_TIMESTAMP_STRUCT _data;
	};

	struct BindingUInt final : Binding{
		BindingUInt( SQLSMALLINT type )ι:	Binding{ type, SQL_C_UBIGINT }{}
		BindingUInt( uint value )ι: Binding{ SQL_BIGINT, SQL_C_UBIGINT }, _data{value}{}
		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data}; }
	private:
		uint _data ;
	};

	struct BindingDateTime final : Binding{
		BindingDateTime( SQLSMALLINT type=SQL_TYPE_TIMESTAMP )ι:Binding{ type, SQL_C_TIMESTAMP, sizeof(SQL_TIMESTAMP_STRUCT) }{}

		BindingDateTime( const optional<DBTimePoint>& value )ι;

		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override { return IsNull() ? Value{} : Value{ GetDateTime() }; }
		SQLLEN BufferLength()Ι override{ return sizeof(_data); }
		//https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/column-size?view=sql-server-ver16&redirectedfrom=MSDN
		//https://wezfurlong.org/blog/2005/Nov/calling-sqlbindparameter-to-bind-sql-timestamp-struct-as-sql-c-type-timestamp-avoiding-a-datetime-overflow/
		SQLULEN Size()Ι override{ return 23; }//23 works with 0
		α DecimalDigits()Ι->SQLSMALLINT override{ return 3; }//https://stackoverflow.com/questions/40918607/cannot-bind-a-sql-type-timestamp-value-using-odbc-with-ms-sql-server-hy104-inv
		α GetDateTime()Ι->DBTimePoint override{ return IsNull() ? DBTimePoint() : Chrono::ToTimePoint( _data.year, (uint8)_data.month, (uint8)_data.day, (uint8)_data.hour, (uint8)_data.minute, (uint8)_data.second, std::chrono::duration_cast<Duration>(std::chrono::nanoseconds(_data.fraction)) );}
		SQL_TIMESTAMP_STRUCT _data;
	};

	struct BindingDouble final : Binding{
		BindingDouble( SQLSMALLINT type=SQL_DOUBLE )ι:	Binding{ type, SQL_C_DOUBLE }{}
		BindingDouble( double value )ι: Binding{ SQL_DOUBLE, SQL_C_DOUBLE },_data{value}{}
		BindingDouble( const optional<double>& value )ι:
			Binding{ SQL_DOUBLE, SQL_C_DOUBLE },_data{value.has_value() ? value.value() : 0.0}{
			if( !value.has_value() )
				Output = SQL_NULL_DATA;
		}

		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data}; }

		α GetDouble()Ι->double override{ return _data; }
	private:
		double _data;
	};
	struct BindingNumeric final : TBinding<SQL_NUMERIC_STRUCT,SQL_NUMERIC,SQL_C_NUMERIC>{
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{GetDouble()}; }
		α GetDouble()Ι->double override{//https://docs.microsoft.com/en-us/sql/odbc/reference/appendixes/retrieve-numeric-data-sql-numeric-struct-kb222831?view=sql-server-ver15
			double divisor = std::pow( 10.0, _data.scale );
			_int value = 0, last=1;
			for( uint i=0; i<SQL_MAX_NUMERIC_LEN; ++i ){
				const int current = _data.val[i];
				const int a = current % 16;
				const int b = current / 16;
				value += last * a;
				last *= 16;
				value += last * b;
				last *= 16;
			}
			return (_data.sign ? 1 : -1)*(double)value/divisor;
		}
	};

	struct BindingFloat final : Binding{
		BindingFloat( SQLSMALLINT type=SQL_FLOAT )ι:	Binding{ type, SQL_C_FLOAT }{}
		BindingFloat( float value )ι: Binding{ SQL_FLOAT, SQL_C_FLOAT },_data{value}{}

		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data}; }

		double GetDouble()Ι override{ return _data; }
	private:
		float _data;
	};

	struct BindingInt16 final : Binding{
		BindingInt16()ι:Binding{ SQL_SMALLINT, SQL_C_SSHORT }{}
		BindingInt16( int16_t value )ι: Binding{ SQL_SMALLINT, SQL_C_SSHORT },_data{value}{}

		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override { return IsNull() ? Value{} : Value{_data}; }
		α GetDouble()Ι->double override{ return _data; }
	private:
		int16_t _data;
	};

	struct BindingUInt8 final : Binding{
		BindingUInt8()ι:Binding{ SQL_TINYINT, SQL_C_TINYINT }{}
		BindingUInt8( uint8_t value )ι: Binding{ SQL_TINYINT, SQL_C_TINYINT },_data{value}{}

		α Data()ι->void* override{ return &_data; }
		α GetValue()Ι->Value override{ return IsNull() ? Value{} : Value{_data}; }

		α GetDouble()Ι->double override{ return _data; }
	private:
		uint8_t _data;
	};

	Ξ Binding::GetBinding( SQLSMALLINT type )ε->up<Binding>{
		up<Binding> pBinding;
		switch( type ){
			case SQL_BIT: pBinding = mu<BindingBit>(); break;
			case SQL_TINYINT: pBinding = mu<BindingUInt8>(); break;
			case SQL_INTEGER: pBinding = mu<BindingInt32>( type ); break;
			case SQL_DECIMAL:  pBinding = mu<BindingDouble>( type ); break;
			case SQL_SMALLINT: pBinding = mu<BindingInt16>(); break;
			case SQL_FLOAT: pBinding = mu<BindingFloat>( type ); break;
			case SQL_REAL: pBinding = mu<BindingDouble>( type ); break;
			case SQL_DOUBLE: pBinding = mu<BindingDouble>( type ); break;
			case SQL_DATETIME: pBinding = mu<BindingDateTime>( type ); break;
			case SQL_BIGINT: pBinding = mu<BindingInt>( type ); break;
			case SQL_TYPE_TIMESTAMP: pBinding = mu<BindingTimeStamp>( type ); break;
			case SQL_NUMERIC:	pBinding = mu<BindingNumeric>(); break;
			case SQL_VARBINARY:{ pBinding = mu<BindingBinary>(); break;}
			case SQL_GUID: { pBinding = mu<BindingGuid>(); break; }
			default: THROW( "Binding type '{}' is not implemented.", type );
		}
		return pBinding;
	}
	using std::get;
	Ξ Binding::Create( Value param )ι->up<Binding>{
		up<Binding> pBinding;
		switch( param.Type() ){
		using enum EValue;
		case Null: pBinding = mu<BindingNull>(); break;
		case String: pBinding = mu<BindingString>( param.get_string() ); break;
		case Bool: pBinding = mu<BindingBit>( param.get_bool() ); break;
		case Bytes: pBinding = mu<BindingBinary>( move( param ) ); break;
		case Int8: pBinding = mu<BindingInt8>( param.get_int8() ); break;
		case Int32:
			pBinding = mu<BindingInt32>( param.get_int32() );
		break;
		case Int64:
			pBinding = mu<BindingInt>( param.get_int() );
		break;
		case UInt32:
			pBinding = mu<BindingInt32>( (int)param.get_uint32() );
		break;
		case UInt64:
			pBinding = mu<BindingInt>( (_int)param.get_uint() );
		break;
		case Double:
			pBinding = mu<BindingDouble>( param.get_double() );
		break;
		case Time:
			pBinding = mu<BindingDateTime>( param.get_time() );
		break;
		default:
			ASSERT( false );
		}
		return pBinding;
	}
	inline BindingDateTime::BindingDateTime( const optional<DBTimePoint>& tp )ι:
		BindingDateTime{}{
		if( !tp.has_value() )
			Output = SQL_NULL_DATA;
		else{
			using namespace std::chrono;
			let date{ floor<days>(*tp) };
			const year_month_day ymd{ date };
			const hh_mm_ss time{ *tp-date };

			_data.year = (SQLSMALLINT)(int)ymd.year();
			_data.month = (SQLUSMALLINT)(unsigned)ymd.month();
			_data.day = (SQLUSMALLINT)(unsigned)ymd.day();
			_data.hour = (SQLUSMALLINT)(unsigned)time.hours().count();
			_data.minute = (SQLUSMALLINT)time.minutes().count();
			_data.second = (SQLUSMALLINT)time.seconds().count();
			_data.fraction = (SQLUINTEGER)duration_cast<nanoseconds>( time.subseconds() ).count();//fraction is nanoseconds (ODBC spec); subseconds() counts system_clock ticks (100ns on Windows) - was 100x low.
		}
	}
}
#undef let
#pragma clang diagnostic pop
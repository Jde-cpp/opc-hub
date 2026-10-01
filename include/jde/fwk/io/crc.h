#pragma once
#ifndef CRC_H
#define CRC_H//gcc precompiled headers
DISABLE_WARNINGS
#include <boost/crc.hpp>
#include <absl/crc/crc32c.h>
ENABLE_WARNINGS
namespace Jde::IO::Crc{
	//the byte-at-a-time table for a reflected 32-bit CRC polynomial.
	consteval α ReflectedTable( uint32_t polynomial )->std::array<uint32_t,256>{
		std::array<uint32_t,256> table{};
		for( uint32_t i=0; i<256; ++i ){
			uint32_t crc = i;
			for( int bit=0; bit<8; ++bit )
				crc = (crc >> 1) ^ ( (crc & 1) ? polynomial : 0 );
			table[i] = crc;
		}
		return table;
	}
	inline constexpr auto crc32_table = ReflectedTable( 0xEDB88320 );//CRC-32 (zip, ethernet)
	inline constexpr auto crc32c_table = ReflectedTable( 0x82F63B78 );//CRC-32C, Castagnoli

	template<const auto& table>
	constexpr α CalcReflected( sv value )->uint32_t{
		uint32_t crc = 0xFFFFFFFF;
		for( char ch : value )
			crc = table[static_cast<uint8_t>(crc) ^ static_cast<uint8_t>(ch)] ^ (crc >> 8);//unsigned casts: bytes ≥0x80 sign-extend & index out of bounds.
		return crc ^ 0xFFFFFFFF;
	}

	//the single crc32 implementation - constexpr so it serves compile-time & runtime callers alike.
	inline constexpr α Calc32( sv value )->unsigned int{ return CalcReflected<crc32_table>( value ); }

	inline constexpr α operator ""_crc32( const char *s, size_t n )->unsigned int{
		return Calc32( sv{s, n} );
	}

	static_assert( "Hello"_crc32 == 0xF7D18982, "CRC32 sanity check failed" );

	//CRC-32C: the table at compile time; at run time abseil's hardware CRC, which the presets' cpuFlags compile in
	//(build/CMakeLists.txt).  uint32_t, not uint32: uint_fast32_t is 64 bits on Linux.
	inline constexpr α Calc32c( sv value )->uint32_t{
		if !consteval{
			return static_cast<uint32_t>( absl::ComputeCrc32c(value) );
		}
		return CalcReflected<crc32c_table>( value );
	}

	static_assert( Calc32c("123456789") == 0xE3069283, "CRC-32C sanity check failed" );
}
namespace Jde{
	Ξ Calc32RunTime( sv value )->unsigned int{ return IO::Crc::Calc32( value ); }
}
#endif
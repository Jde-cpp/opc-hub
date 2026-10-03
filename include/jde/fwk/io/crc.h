#pragma once
#ifndef CRC_H
#define CRC_H//gcc precompiled headers
DISABLE_WARNINGS
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
	inline constexpr auto crc32c_table = ReflectedTable( 0x82F63B78 );//CRC-32C, Castagnoli

	template<const auto& table>
	constexpr α CalcReflected( sv value )->uint32_t{
		uint32_t crc = 0xFFFFFFFF;
		for( char ch : value )
			crc = table[static_cast<uint8_t>(crc) ^ static_cast<uint8_t>(ch)] ^ (crc >> 8);//unsigned casts: bytes ≥0x80 sign-extend & index out of bounds.
		return crc ^ 0xFFFFFFFF;
	}

	//CRC-32C: the table at compile time; at run time abseil's hardware CRC, which the presets' cpuFlags compile in
	//(build/CMakeLists.txt).  uint32_t, not uint32: uint_fast32_t is 64 bits on Linux.
	inline constexpr α Calc32c( sv value )->uint32_t{
		if !consteval{
			return static_cast<uint32_t>( absl::ComputeCrc32c(value) );
		}
		return CalcReflected<crc32c_table>( value );
	}
	//Calc32c continued over more bytes:  Extend32c( Calc32c(a), b )==Calc32c( a+b ), so a stream needs no whole buffer.
	inline α Extend32c( uint32_t crc, sv more )->uint32_t{
		return static_cast<uint32_t>( absl::ExtendCrc32c(absl::crc32c_t{crc}, more) );
	}

	static_assert( Calc32c("123456789") == 0xE3069283, "CRC-32C sanity check failed" );
}
#endif
#include <jde/fwk/io/crc.h>

namespace Jde::Tests{
	using IO::Crc::Calc32c;
	using Bytes = std::array<char,32>;

	constexpr α Fill( auto&& byte )->Bytes{
		Bytes bytes{};
		for( uint i=0; i<bytes.size(); ++i )
			bytes[i] = static_cast<char>( byte(i) );
		return bytes;
	}
	constexpr α View( const Bytes& bytes )->sv{ return {bytes.data(), bytes.size()}; }

	//the standard check value, then RFC 3720 B.4's vectors - the last three carry bytes ≥0x80 or a full byte range.
	constexpr sv _check{ "123456789" };
	constexpr Bytes _zeros{};
	constexpr Bytes _ones{ Fill([](uint){ return 0xFF; }) };
	constexpr Bytes _ascending{ Fill([](uint i){ return i; }) };
	constexpr Bytes _descending{ Fill([](uint i){ return 31-i; }) };

	static_assert( Calc32c(_check)==0xE3069283 );
	static_assert( Calc32c({})==0 );
	static_assert( Calc32c(View(_zeros))==0x8A9136AA );
	static_assert( Calc32c(View(_ones))==0x62A8AB43 );
	static_assert( Calc32c(View(_ascending))==0x46DD794E );
	static_assert( Calc32c(View(_descending))==0x113FDB5C );

	//a string built at run time, so the call can only take the absl path.
	Ω RunTime( sv value )->uint32_t{ return Calc32c( string{value} ); }

	TEST( CrcTests, Calc32cRunTime ){
		EXPECT_EQ( RunTime(_check), 0xE3069283u );
		EXPECT_EQ( RunTime({}), 0u );
		EXPECT_EQ( RunTime(View(_zeros)), 0x8A9136AAu );
		EXPECT_EQ( RunTime(View(_ones)), 0x62A8AB43u );
		EXPECT_EQ( RunTime(View(_ascending)), 0x46DD794Eu );
		EXPECT_EQ( RunTime(View(_descending)), 0x113FDB5Cu );
	}
}

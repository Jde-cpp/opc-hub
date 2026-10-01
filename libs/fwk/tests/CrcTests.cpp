#include <jde/fwk/io/crc.h>

namespace Jde::Tests{
	using IO::Crc::Calc32c;
	using Bytes = std::array<char,32>;

	template<uint N=32>
	constexpr α Fill( auto&& byte )->std::array<char,N>{
		std::array<char,N> bytes{};
		for( uint i=0; i<bytes.size(); ++i )
			bytes[i] = static_cast<char>( byte(i) );
		return bytes;
	}
	constexpr α View( const auto& bytes )->sv{ return {bytes.data(), bytes.size()}; }

	//the standard check value, then RFC 3720 B.4's 32-byte vectors.
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

	//Up to 64 bytes abseil computes inline in the caller; longer inputs reach abseil_dll's own paths - under 256 bytes, under
	//2048, and the multi-stream PCLMUL one - each from an aligned and an unaligned start.  Expected values: the table's.
	constexpr auto _large = Fill<5003>( [](uint i){ return i*131 + (i>>8); } );//every byte value, not 256-periodic
	struct LargeCase{ uint offset; uint size; uint32_t crc; };
	constexpr auto _largeCases = []{
		std::array<LargeCase,6> cases{};
		uint i{};
		for( uint size : {65, 1000, 5000} )
			for( uint offset : {0, 3} )
				cases[i++] = { offset, size, Calc32c(View(_large).substr(offset, size)) };
		return cases;
	}();

	TEST( CrcTests, Calc32cRunTimeLarge ){
		const string data{ View(_large) };
		for( auto& c : _largeCases )
			EXPECT_EQ( Calc32c(sv{data}.substr(c.offset, c.size)), c.crc ) << "offset=" << c.offset << " size=" << c.size;
	}
}

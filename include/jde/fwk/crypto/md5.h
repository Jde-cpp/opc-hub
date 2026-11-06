#pragma once
#include <bit>

namespace Jde::Crypto{
	namespace Internal{
		constexpr uint32_t Md5K[64]{
			0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
			0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
			0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
			0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
			0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
			0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
			0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
			0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
		constexpr uint32_t Md5Shift[16]{ 7,12,17,22, 5,9,14,20, 4,11,16,23, 6,10,15,21 };
	}
	//RFC 1321 for constant evaluation, the same bytes CalcMd5 gives:  a log call's template id is computed by the compiler
	//(Logging::Format).  Every id is shared with the web, the archives and the wire, so the RFC's own test vectors are
	//pinned below and Md5Tests compares it with CalcMd5.  CalcMd5 is the faster at run time.
	constexpr α Md5( sv s )ι->StringMd5{
		using namespace Internal;
		uint32_t h[4]{ 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
		const uint64_t size = s.size(), padded = ( (size+8)/64+1 )*64;//the 0x80 byte and the 8-byte bit count, to a whole block.
		for( uint64_t offset=0; offset<padded; offset+=64 ){
			uint32_t m[16]{};
			for( uint64_t i=0; i<64; ++i ){
				const uint64_t at = offset+i;
				const uint8_t b = at<size ? (uint8_t)s[at] : at==size ? 0x80 : at>=padded-8 ? (uint8_t)( (size*8) >> (8*(at-(padded-8))) ) : 0;
				m[i/4] |= (uint32_t)b << ( 8*(i%4) );
			}
			uint32_t a=h[0], b=h[1], c=h[2], d=h[3];
			for( uint32_t i=0; i<64; ++i ){
				uint32_t f, g;
				if( i<16 ){ f = (b&c) | (~b&d); g = i; }
				else if( i<32 ){ f = (d&b) | (~d&c); g = (5*i+1)%16; }
				else if( i<48 ){ f = b^c^d; g = (3*i+5)%16; }
				else{ f = c^(b|~d); g = (7*i)%16; }
				f += a+Md5K[i]+m[g]; a = d; d = c; c = b;
				b += std::rotl( f, (int)Md5Shift[(i/16)*4+i%4] );
			}
			h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
		}
		uint8_t digest[16]{};
		for( uint i=0; i<16; ++i )
			digest[i] = (uint8_t)( h[i/4] >> (8*(i%4)) );
		return StringMd5{ digest };
	}

	namespace Internal{
		consteval α Md5Is( sv text, sv hex )ι->bool{
			const auto id = Md5( text );
			constexpr sv digits{ "0123456789abcdef" };
			for( uint i=0; i<16; ++i ){
				if( digits[id.data[i]>>4]!=hex[2*i] || digits[id.data[i]&15]!=hex[2*i+1] )
					return false;
			}
			return true;
		}
		static_assert( Md5Is("", "d41d8cd98f00b204e9800998ecf8427e") );//RFC 1321 A.5
		static_assert( Md5Is("a", "0cc175b9c0f1b6a831c399e269772661") );
		static_assert( Md5Is("abc", "900150983cd24fb0d6963f7d28e17f72") );
		static_assert( Md5Is("message digest", "f96b697d7cb7938d525a2f31aaf161d0") );
		static_assert( Md5Is("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b") );
		static_assert( Md5Is("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f") );
		static_assert( Md5Is("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a") );
		static_assert( Md5("")==EmptyStringMd5 );
	}
}
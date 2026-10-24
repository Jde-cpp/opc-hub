#pragma once
#include <span>
#define Φ Γ auto

namespace Jde::Crypto{
	using MD5 = boost::uuids::uuid;
	Φ CalcMd5( std::span<const byte> data )ε->MD5;
	Ŧ CalcMd5( const T& content )ε->MD5 requires requires{ content.data(); content.size(); }{ return CalcMd5( std::as_bytes(std::span{content.data(), content.size()}) ); }//by reference - the old by-value form copied every Modulus it hashed.
	Φ Random( std::span<byte> bytes )ε->void;//throws on entropy failure.
	Ŧ Random()ε->T{ T y{}; Random( std::as_writable_bytes(std::span{&y, 1}) ); return y; }
}
#undef Φ
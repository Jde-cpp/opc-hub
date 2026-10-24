#pragma once
#include <span>
#include "digest.h"
#define Φ Γ auto

namespace Jde::Crypto{
	using Modulus = vector<byte>;
	using Exponent = vector<byte>;
	using Signature = vector<byte>;

	//An RSA public key as its (modulus, exponent) - the enrollment identity:  access_users is keyed by the modulus, a Jwt carries both.
	struct Γ PublicKey{
		PublicKey() = default;
		PublicKey( Crypto::Modulus modulus, Crypto::Exponent exponent )ι: Modulus{move(modulus)}, Exponent{move(exponent)} {}
		α operator==( const PublicKey& other )Ι->bool{ return Modulus == other.Modulus && Exponent == other.Exponent; }
		α operator<( const PublicKey& other )Ι->bool{ return Exponent == other.Exponent ? Modulus < other.Modulus : Exponent < other.Exponent; }
		α Hash32()Ι->uint32_t;//the big-endian fold of md5(modulus) - a session display id, stable across versions.
		α ExponentInt()Ι->uint32_t;
		α ModulusHex()Ε->string;//access_users' column.
		Crypto::Modulus Modulus;
		Crypto::Exponent Exponent;
	};
	Φ ExtractPublicKey( std::span<const byte> certificate, SRCE )ε->PublicKey;//the key a DER certificate carries.
	Φ ReadPublicKey( const fs::path& pem, SRCE )ε->PublicKey;
	Φ ToDer( const PublicKey& key, SRCE )ε->vector<byte>;//SubjectPublicKeyInfo - what Fingerprint hashes.
	Φ Fingerprint( const PublicKey& key, SRCE )ε->MD5;
	//sha-256 over `content`, PKCS#1 v1.5 - what PrivateKey::Sign produces.  A mismatch throws OpenSslException "Signature
	//verification failed." - an expected auth outcome, so it is named rather than echoed as a bare rc;  a negative rc is a malfunction.
	Φ Verify( const PublicKey& key, str content, const Signature& signature, SRCE )ε->void;
}
#undef Φ
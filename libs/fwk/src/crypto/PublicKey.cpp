#include <jde/fwk/crypto/PublicKey.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <jde/fwk/str.h>
#include "ossl.h"

#define let const auto

namespace Jde{
	using namespace Crypto::Internal;
	namespace Crypto{
		//https://stackoverflow.com/questions/1986888/how-to-compute-a-32-bit-fingerprint-of-a-certificate
		α PublicKey::Hash32()Ι->uint32_t{
			uint32_t hash{};
			for( auto b : CalcMd5(Modulus) )
				hash = ( hash << 8 ) | (uint32_t)b;
			return hash;
		}
		α PublicKey::ExponentInt()Ι->uint32_t{
			uint32_t exponent{};
			for( let b : Exponent )
				exponent = ( exponent<<8 ) | std::to_integer<uint32_t>( b );
			return exponent;
		}
		α PublicKey::ModulusHex()Ε->string{
			auto modHex = Str::ToHex( Modulus );
			THROW_IF( modHex.size() > 1024, "modulus {} is too long. max length: {}", modHex.size(), 1024 );
			return modHex;
		}

		Ω bnBytes( const BIGNUM* n )ι->vector<byte>{
			vector<byte> y( BN_num_bytes(n) );//a macro, not a function.
			::BN_bn2bin( n, (unsigned char*)y.data() );
			return y;
		}
		Ω toPublicKey( const EVP_PKEY* key, SL sl )ε->PublicKey{
			BIGNUM* n{}; BIGNUM* e{};
			OSSL_CALL( ::EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_N, &n), sl );
			const BNPtr pN{ n, ::BN_free };
			OSSL_CALL( ::EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_E, &e), sl );
			const BNPtr pE{ e, ::BN_free };
			return { bnBytes(n), bnBytes(e) };
		}
		//https://stackoverflow.com/questions/28770426/rsa-public-key-conversion-with-just-modulus
		//OSSL_PARAM_BLD:  a BN param built by hand is native-endian, and the old code set it from a BIGNUM afterwards - into the caller's modulus.
		Ω toEvp( const PublicKey& key, SL sl )ε->KeyPtr{
			let n = ToBigNum( key.Modulus, sl ), e = ToBigNum( key.Exponent, sl );
			const up<OSSL_PARAM_BLD, decltype(&::OSSL_PARAM_BLD_free)> bld{ ::OSSL_PARAM_BLD_new(), ::OSSL_PARAM_BLD_free }; OSSL_CHECK( bld, sl );
			OSSL_CALL( ::OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_RSA_N, n.get()), sl );
			OSSL_CALL( ::OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_RSA_E, e.get()), sl );
			const up<OSSL_PARAM, decltype(&::OSSL_PARAM_free)> params{ ::OSSL_PARAM_BLD_to_param(bld.get()), ::OSSL_PARAM_free }; OSSL_CHECK( params, sl );
			const CtxPtr ctx{ ::EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), ::EVP_PKEY_CTX_free }; OSSL_CHECK( ctx, sl );
			OSSL_CALL( ::EVP_PKEY_fromdata_init(ctx.get()), sl );
			EVP_PKEY* evp{};
			OSSL_CALL( ::EVP_PKEY_fromdata(ctx.get(), &evp, EVP_PKEY_PUBLIC_KEY, params.get()), sl );
			return { evp, ::EVP_PKEY_free };
		}
	}

	α Crypto::ExtractPublicKey( std::span<const byte> certificate, SL sl )ε->PublicKey{
		let cert = ParseDer( certificate, sl );
		const KeyPtr key{ ::X509_get_pubkey(cert.get()), ::EVP_PKEY_free }; OSSL_CHECK( key, sl );
		return toPublicKey( key.get(), sl );
	}
	α Crypto::ReadPublicKey( const fs::path& pem, SL sl )ε->PublicKey{
		const KeyPtr key{ ::PEM_read_bio_PUBKEY(ReadFile(pem, sl).get(), nullptr, nullptr, nullptr), ::EVP_PKEY_free };
		THROW_IFX( !key, OpenSslException(Ƒ("Could not parse public key '{}'", pem.string()), sl) );//not OSSL_CHECK - name the file, as ReadCertificate does.
		return toPublicKey( key.get(), sl );
	}
	α Crypto::ToDer( const PublicKey& key, SL sl )ε->vector<byte>{
		let evp = toEvp( key, sl );
		auto len = ::i2d_PUBKEY( evp.get(), nullptr ); THROW_IFX( len<=0, OpenSslException("i2d_PUBKEY failed", sl) );
		vector<byte> y( len );
		auto p = (unsigned char*)y.data();
		len = ::i2d_PUBKEY( evp.get(), &p ); THROW_IFX( len<=0, OpenSslException("i2d_PUBKEY failed", sl) );
		return y;
	}
	α Crypto::Fingerprint( const PublicKey& key, SL sl )ε->MD5{
		return CalcMd5( ToDer(key, sl) );
	}
	α Crypto::Verify( const PublicKey& key, str content, const Signature& signature, SL sl )ε->void{
		THROW_IFSL( key.Modulus.empty(), "key.Modulus is empty" );
		THROW_IFSL( key.Exponent.empty(), "key.Exponent is empty" );
		let evp = toEvp( key, sl );
		const MDCtxPtr ctx{ ::EVP_MD_CTX_new(), ::EVP_MD_CTX_free }; OSSL_CHECK( ctx, sl );
		OSSL_CALL( ::EVP_DigestVerifyInit(ctx.get(), nullptr, ::EVP_sha256(), nullptr, evp.get()), sl );
		let rc = ::EVP_DigestVerify( ctx.get(), (const unsigned char*)signature.data(), signature.size(), (const unsigned char*)content.data(), content.size() );
		if( rc!=1 )//0 is a semantic outcome (signature mismatch - an expected auth event), negative a malfunction.
			throw OpenSslException{ rc==0 ? string{"Signature verification failed."} : Ƒ("EVP_DigestVerify -> {}", rc), sl };
	}
}
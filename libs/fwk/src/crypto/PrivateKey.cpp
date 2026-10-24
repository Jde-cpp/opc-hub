#include <jde/fwk/crypto/PrivateKey.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include "ossl.h"

#define let const auto

namespace Jde::Crypto{
	using namespace Internal;

	PrivateKey::PrivateKey( EVP_PKEY* key )ι:
		_key{ key, ::EVP_PKEY_free }
	{}

	PrivateKey::PrivateKey( const fs::path& pem, str passcode, SL sl )ε:
		PrivateKey{ ::PEM_read_bio_PrivateKey(ReadFile(pem, sl).get(), nullptr, nullptr, (void*)passcode.c_str()) }{
		THROW_IFX( !_key, OpenSslException(Ƒ("Could not read private key '{}'{}", pem.string(), passcode.empty() ? " (no passcode configured - an encrypted key needs privateKey.passcode)" : " (wrong passcode, or the key is not encrypted with this one)"), sl) );
	}

	PrivateKey::PrivateKey( std::span<const byte> der, SL sl )ε:
		PrivateKey{ ::d2i_PrivateKey_bio(MemBio(der, sl).get(), nullptr) }{
		THROW_IFX( !_key, OpenSslException("d2i_PrivateKey_bio", sl) );
	}

	α PrivateKey::Generate( SL sl )ε->PrivateKey{
		const CtxPtr ctx{ ::EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), ::EVP_PKEY_CTX_free }; OSSL_CHECK( ctx, sl );
		OSSL_CALL( ::EVP_PKEY_keygen_init(ctx.get()), sl );
		unsigned bits{ 2048 }, e{ 65537 };
		OSSL_PARAM params[]{ OSSL_PARAM_construct_uint(OSSL_PKEY_PARAM_RSA_BITS, &bits), OSSL_PARAM_construct_uint(OSSL_PKEY_PARAM_RSA_E, &e), OSSL_PARAM_construct_end() };
		OSSL_CALL( ::EVP_PKEY_CTX_set_params(ctx.get(), params), sl );
		EVP_PKEY* key{};
		OSSL_CALL( ::EVP_PKEY_generate(ctx.get(), &key), sl );
		return PrivateKey{ key };
	}

	α PrivateKey::ReadUnencrypted( const fs::path& pem, SL sl )ε->optional<PrivateKey>{
		//a callback that supplies no passphrase:  an encrypted key fails on it - the case on every start once EncryptPrivateKey has
		//run, so it neither throws nor logs - and a clear one never calls it.
		EVP_PKEY* clear = ::PEM_read_bio_PrivateKey( ReadFile(pem, sl).get(), nullptr, [](char*, int, int, void*)->int{ return -1; }, nullptr );
		if( !clear ){
			::ERR_clear_error();
			return {};
		}
		return PrivateKey{ clear };
	}

	α PrivateKey::Sign( sv content, SL sl )Ε->Signature{
		const MDCtxPtr ctx{ ::EVP_MD_CTX_new(), ::EVP_MD_CTX_free }; OSSL_CHECK( ctx, sl );
		OSSL_CALL( ::EVP_DigestSignInit(ctx.get(), nullptr, ::EVP_sha256(), nullptr, _key.get()), sl );
		size_t length{};
		OSSL_CALL( ::EVP_DigestSign(ctx.get(), nullptr, &length, (const unsigned char*)content.data(), content.size()), sl );
		Signature y( length );
		OSSL_CALL( ::EVP_DigestSign(ctx.get(), (unsigned char*)y.data(), &length, (const unsigned char*)content.data(), content.size()), sl );
		y.resize( length );
		return y;
	}

	α PrivateKey::Der( SL sl )Ε->vector<byte>{
		auto len = ::i2d_PrivateKey( _key.get(), nullptr ); THROW_IFX( len<=0, OpenSslException("i2d_PrivateKey failed", sl) );
		vector<byte> y( len );
		auto p = (unsigned char*)y.data();
		len = ::i2d_PrivateKey( _key.get(), &p ); THROW_IFX( len<=0, OpenSslException("i2d_PrivateKey failed", sl) );
		return y;
	}

	α PrivateKey::Write( const fs::path& pem, str passcode, SL sl )Ε->void{
		let cipher = passcode.empty() ? nullptr : ::EVP_aes_256_cbc();//openssl silently ignores the passphrase & writes cleartext when the cipher is null.
		OSSL_CALL( ::PEM_write_bio_PrivateKey(WriteFile(pem, sl).get(), _key.get(), cipher, passcode.empty() ? nullptr : (const unsigned char*)passcode.data(), (int)passcode.size(), nullptr, nullptr), sl );
	}

	α PrivateKey::WritePublic( const fs::path& pem, SL sl )Ε->void{
		OSSL_CALL( ::PEM_write_bio_PUBKEY(WriteFile(pem, sl).get(), _key.get()), sl );
	}
}